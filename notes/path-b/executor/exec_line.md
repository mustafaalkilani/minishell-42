# exec_line.c

The top of the execution engine. It decides between the three shapes a command
line can take — an interrupted heredoc, a lone builtin that must run in the
shell process, or a real pipeline of forked children — and it owns every pipe
file descriptor created along the way. Everything about fd hygiene and about
where a builtin's side effects survive is decided in this file.

## Walkthrough

### `static void pipeline_child(t_cmd *cmd, t_shell *sh, t_pipeline *pl)`

This runs **only in the child**, immediately after `fork()` returned 0. It never
returns: the last statement is `child_exec`, which always ends in `exit()` or
`execve()`.

```c
if (pl->prev_read != -1)
{
    dup2(pl->prev_read, STDIN_FILENO);
    close(pl->prev_read);
}
```
`prev_read` is the read end of the pipe created for the *previous* command in
the loop; `-1` means "I am the first command, keep the shell's stdin". `dup2`
makes fd 0 a second reference to the same open file description, then the
original fd is closed because we no longer need two names for it.

The `close` is not cosmetic. If it were omitted, this child would still hold
`prev_read` open. That is a read end, so it would not by itself stop the writer
from seeing EOF — but the fd would survive `execve` (nothing here sets
`FD_CLOEXEC`) and the external program would start life with a stray descriptor
pointing at a pipe. In a long pipeline every child would accumulate one more,
and `ulimit -n` becomes reachable. Closing it here is also what keeps the
descriptor table of the exec'd program identical to what bash would give it.

```c
if (cmd->next)
{
    close(pl->pipefd[0]);
    dup2(pl->pipefd[1], STDOUT_FILENO);
    close(pl->pipefd[1]);
}
```
`cmd->next` is the test for "I am not the last stage, so I write into a pipe".
The order matters and is worth defending line by line:

- `close(pl->pipefd[0])` drops the read end of *my own* pipe. This one is the
  classic deadlock. The next command in the pipeline is going to read from
  `pipefd[0]`; it only sees EOF when **every** copy of the write end is closed.
  That is not the fd being closed here — but the symmetric argument applies to
  the reader: if this child kept a read end open on a pipe that nobody writes
  to, and it later read from it, it would block forever. More concretely, `head
  -1 | cat` relies on each process holding the minimum set of ends. Keeping an
  unused read end open is exactly the mistake that makes `cat | cat` never
  finish when the terminal sends EOF.
- `dup2(pl->pipefd[1], STDOUT_FILENO)` points fd 1 at the write end.
- `close(pl->pipefd[1])` removes the duplicate name. If this were skipped, this
  process would hold **two** write-end references (fd 1 and `pipefd[1]`).
  Closing fd 1 later — or the program exiting — would still leave one open
  until process death, which in practice is survivable, but again the fd leaks
  across `execve` into the child program.

```c
close_other_heredocs(sh->current_cmds, cmd);
```
The pipe fds are now in place, so the last thing left to clean up before the
command takes over is the heredocs belonging to the **other** stages.

All heredocs for the whole line were drained into pipes before any fork (see
`collect_heredocs`), because their bodies are typed interactively and only the
shell process can read them. The consequence is that a child forked for
`cat << A | cat << B` inherits B's heredoc read fd as well as its own. Its own
is consumed and closed by `apply_redirs` inside `child_exec`; the other one had
nothing to close it and stayed open across `execve`:

```
$ printf 'cat << A | cat << B | ls /proc/self/fd\nx\nA\ny\nB\n' | ./minishell
0
1
2
3
4      <-- leaked
5      <-- leaked
```
bash prints only `0` through `3`, and so does minishell now. The leak never
deadlocked anything — every heredoc pipe's write end was closed inside
`read_heredoc`, so EOF was always guaranteed — but the descriptors were
genuinely visible in the exec'd program.

`sh->current_cmds` is what makes this reachable: `exec_line` stores the head of
the command list there before doing anything else, so a child holding only its
own `t_cmd *` can still walk the whole line. `close_other_heredocs` skips the
node equal to `cmd` and closes every heredoc fd in the rest.

Placement matters in one direction only. It must be in the **child**, after the
fork: the parent still needs those fds so the stages that own them can inherit
them, and the parent's own copies are closed later by `free_redirs` when
`process_line` frees the command list. Within the child it could sit anywhere
before `child_exec`; putting it last keeps the pipe-fd block above it as one
unbroken piece of reasoning.

```c
child_exec(cmd, sh);
```
Hands over to `exec_child.c`, which applies this command's own redirections,
resets the signal dispositions, and execs. Control never comes back.

### `static void pipeline_parent(t_cmd *cmd, t_pipeline *pl)`

The mirror image, running in the shell process right after the fork.

```c
if (pl->prev_read != -1)
    close(pl->prev_read);
```
The parent handed `prev_read` to the child by inheritance; it has no further
use for it. This close is **mandatory**, not tidiness. The child that reads
from this pipe only sees EOF when all write ends are gone, and the parent
holding a *read* end is the other half of the problem: if the parent kept every
`prev_read` alive for the duration of the pipeline, a three-stage pipeline
would end with the shell owning descriptors it can never use, and on a long
`while` of pipelines the shell would run out of fds. Closing it immediately
after the fork is the standard discipline: the parent's window of ownership is
exactly one `fork` call wide.

```c
if (cmd->next)
{
    close(pl->pipefd[1]);
    pl->prev_read = pl->pipefd[0];
}
```
This is **the** deadlock close. The write end of the current pipe now exists in
the child (as its stdout). If the parent kept its copy open, the next child —
which reads `pipefd[0]` — would block forever on `read`, because a pipe reports
EOF only when the reference count of write ends hits zero. `cat | grep x` would
simply hang. Every evaluator asks about this line.

The read end is *not* closed: it is stashed in `pl->prev_read` so the next loop
iteration can pass it to the next child, which then dups it onto stdin. It is
closed one iteration later by the `if (pl->prev_read != -1) close(...)` above.
So each `pipefd[0]` has a lifetime of exactly one loop iteration in the parent.

```c
else
    pl->prev_read = -1;
```
The last command in the pipeline created no pipe, so there is nothing to carry
forward. Resetting to `-1` keeps the invariant that `prev_read` is either a
live fd or the sentinel, which is what the `!= -1` guards rely on.

### `static int run_pipeline(t_cmd *cmds, t_shell *sh)`

```c
pl.prev_read = -1;
pl.last_pid = -1;
```
First command inherits the shell's stdin; `last_pid` starts invalid so that
`wait_for_all` cannot accidentally match a real pid before any fork happened.

```c
signals_ignore();
```
Set **before** the loop, in the parent, so that while foreground children run
the shell itself ignores SIGINT and SIGQUIT. Two things follow from this.
First, ctrl-C reaches the whole foreground process group, so the children die
and the shell survives — which is exactly bash's behaviour. Second, because the
children are forked *after* this call, they start with SIG_IGN inherited; that
is why `child_exec` must call `signals_setup_child()` to put SIG_DFL back.
Ignored dispositions survive `execve` (unlike handlers, which are reset to
default), so without that reset `cat` with no arguments could not be killed by
ctrl-C at all.

The parent is never restored to interactive here; `shell_loop` in `srcs/main.c`
calls `signals_setup_interactive()` at the top of every iteration, so the
restore happens there.

```c
while (cmds)
{
    if (cmds->next && pipe(pl.pipefd) < 0)
        return (1);
```
A pipe is created only when there is a next command. Creating it before the
fork is required: both processes must see the same pipe, and the only way to
share it is for it to already exist at `fork` time.

On `pipe` failure we return 1 immediately. Be honest about this path: any
children already forked in earlier iterations are **not** waited for, and
`pl.prev_read` is not closed. That is a zombie plus an fd leak on an
out-of-descriptors condition. It is an extreme edge case (it needs the process
to be at its fd limit) and it does not affect normal grading, but if an
evaluator asks "is there any path where you fork and never wait", this is it.

```c
    pid = fork();
    if (pid < 0)
        return (1);
```
Same caveat: a failed `fork` mid-pipeline abandons the earlier children
unreaped and leaks `pl.prev_read` and both ends of the pipe just created.

```c
    if (pid == 0)
        pipeline_child(cmds, sh, &pl);
```
In the child, `pipeline_child` never returns, so the lines below are parent-only
even though they are not inside an `else`. This is a deliberate idiom: it keeps
the nesting shallow enough for Norminette while remaining correct, precisely
because `child_exec` is a no-return function.

```c
    pl.last_pid = pid;
    pipeline_parent(cmds, &pl);
    cmds = cmds->next;
}
return (wait_for_all(pl.last_pid));
```
`last_pid` is overwritten each iteration, so after the loop it holds the pid of
the **last** stage. That is the one whose exit status becomes `$?`, matching
bash's pipeline semantics (without `pipefail`, the status of a pipeline is the
status of its rightmost command). `wait_for_all` still reaps every child, so no
zombies remain; only the reported status is filtered by pid.

Note that all forks happen before any wait. That is required for concurrency: a
pipeline works because the stages run at the same time, feeding each other. If
we waited for stage 1 before forking stage 2, `yes | head -1` would fill the
pipe buffer and block forever.

### `static int run_parent_builtin(t_cmd *cmd, t_shell *sh)`

```c
saved_in = dup(STDIN_FILENO);
saved_out = dup(STDOUT_FILENO);
```
The builtin is about to run **in the shell process**, so its redirections would
otherwise be permanent. `dup` returns the lowest free fd, typically 3 and 4, and
each is a second reference to the terminal (or to whatever the shell's stdio
currently is). Return values are not checked here; if `dup` returned -1 the
later `dup2(-1, ...)` would fail silently and stdio would stay redirected. In
practice `dup` only fails at the fd limit, and the codebase treats that as
unreachable, but it is an unchecked call and worth acknowledging.

```c
if (apply_redirs(cmd) < 0)
    status = 1;
else
    status = run_builtin(cmd, sh);
```
Redirections first, then the builtin, so `export > file` and `env > out` behave.
A failed redirection (`echo hi > /nonexistent/x`) short-circuits: the builtin is
not run at all and the status is 1, matching bash. `apply_redirs` has already
printed the diagnostic.

```c
dup2(saved_in, STDIN_FILENO);
dup2(saved_out, STDOUT_FILENO);
close(saved_in);
close(saved_out);
return (status);
```
Restore then close. `dup2` silently closes its target first, so fd 0 and fd 1 —
which currently point at the redirection targets — are released here; that is
where the file opened by `apply_redirs` finally loses its last reference (the
`open`ed fd itself was already closed inside `apply_one` right after its
`dup2`). Then the backups are closed so fds 3 and 4 do not accumulate one pair
per builtin invocation. Forgetting these two `close` calls is the classic slow
fd leak: run `pwd > /dev/null` a few thousand times and the shell dies.

One asymmetry to be ready for: if the builtin is `exit`, `builtin_exit` calls
`exit()` directly and these four lines never run. That is harmless — the process
is going away and the kernel closes everything — but it does mean `exit` is the
one builtin whose redirection is never undone.

### `int exec_line(t_cmd *cmds, t_shell *sh)`

```c
sh->current_cmds = cmds;
```
Stores the command list on the shell struct, and it has two consumers. The
original one is `builtin_exit`'s `shell_cleanup`, which frees the list before
calling `exit()`; without it, `exit` inside a command would leak the whole
`t_cmd` list because the owner (`process_line`) never gets to run its
`free_cmds`. The second is `pipeline_child`, which needs the head of the list to
find the heredoc fds belonging to the other stages — a child is handed only its
own `t_cmd *`, so `sh` is its only route back to the rest of the line.

It is assigned first, before `collect_heredocs`, so both consumers see it no
matter which path the line takes.

```c
if (collect_heredocs(cmds, sh) < 0)
    return (EXIT_SIG_BASE + SIGINT);
```
Heredocs are drained **before** anything forks. Two reasons. First, they read
from the terminal, and only one process can sensibly own the terminal at a
time; if each child read its own heredoc they would compete for stdin. Second,
ctrl-C during a heredoc must abandon the line and return to the prompt, which
is interactive-parent behaviour — doing it in a child would require signalling
the result back.

A negative return means the user pressed ctrl-C, and the status is
`128 + SIGINT` = **130**, which is what bash reports for an interrupted
heredoc. Note this is a `return`, not an exit: the shell stays alive and no
command is run.

```c
if (!cmds->next && cmds->argv[0] && is_builtin(cmds->argv[0]))
    return (run_parent_builtin(cmds, sh));
```
The single most-probed rule in the file. `!cmds->next` means "not part of a
pipeline". A lone builtin runs in the parent so that `cd`, `export` and `unset`
mutate the shell's own state and persist to the next prompt. Inside a pipeline
the same builtin is reached through `child_exec` instead and its effects are
discarded when the child exits — which is also what bash does, and is why
`export A=1 | cat` then `echo $A` prints nothing in both shells.

`cmds->argv[0]` is checked before `is_builtin` because a command can legitimately
have an empty argv (for example `> out` alone, or `$EMPTY`), and `argv[0]` would
then be `NULL`. `cmd_new` always allocates a one-element NULL-terminated array,
so `argv` itself is never `NULL` and the dereference is safe.

```c
if (!cmds->next && !cmds->argv[0] && !cmds->redirs)
    return (EXIT_OK);
```
Nothing to run and nothing to open — for instance a line that expanded to
nothing at all. Forking for this would be pointless, so we return 0 directly.
Bash also reports 0 here.

The complementary case is deliberate: `> out` with no command **does** fall
through to `run_pipeline`, forks a child, and that child's `child_exec` runs
`apply_redirs` (creating/truncating the file) and then hits
`if (!cmd->argv[0]) exit(EXIT_OK)`. So the file is created and the status is 0.
Bash performs the same redirection in the parent instead of forking, but the
observable result — file created, `$?` is 0, or `$?` is 1 with a diagnostic if
the open fails — is identical.

```c
return (run_pipeline(cmds, sh));
```
Everything else: external commands, pipelines, and builtins inside pipelines.

## Sequence

What happens, in order, for `cat < in | grep x > out`.

1. `process_line` (srcs/main.c) has already produced two `t_cmd` nodes. Node A
   is `argv = {"cat"}` with one `t_redir` of type `R_IN`, filename `in`.
   Node B is `argv = {"grep","x"}` with one `t_redir` of type `R_OUT`,
   filename `out`. `A->next == B`, `B->next == NULL`.
2. `exec_line` stores `sh->current_cmds = A`.
3. `collect_heredocs` walks both nodes, finds no `R_HEREDOC`, dups and restores
   stdin around a no-op loop, and returns 0.
4. `A->next` is non-NULL, so neither the lone-builtin branch nor the empty-line
   branch is taken. `run_pipeline(A, sh)` is called.
5. `pl.prev_read = -1`, `pl.last_pid = -1`. `signals_ignore()` sets the shell's
   SIGINT and SIGQUIT to `SIG_IGN`.
6. **Iteration 1, cmds = A.** `A->next` is non-NULL, so `pipe(pl.pipefd)`
   succeeds; say it gives `pipefd[0] = 3` (read) and `pipefd[1] = 4` (write).
   Both are open in the shell.
7. `fork()`. Both processes now hold fds 3 and 4.
8. **Child A** enters `pipeline_child`. `prev_read` is -1, so stdin is left
   alone. `A->next` is non-NULL, so it `close(3)` (it will never read from this
   pipe), `dup2(4, 1)` (stdout is now the pipe), `close(4)` (the duplicate
   name). Child A now holds exactly one write end, as fd 1.
   `close_other_heredocs(A, A)` then walks A and B; A is skipped as `self` and B
   has no `R_HEREDOC` redirection, so nothing is closed. In a line with heredocs
   this is where B's read end would be dropped.
9. Child A calls `child_exec`: `signals_setup_child()` restores SIG_DFL;
   `apply_redirs` opens `in` read-only as fd 3 (lowest free), `dup2(3, 0)`,
   `close(3)`. `resolve_command("cat")` walks PATH and returns `/usr/bin/cat`.
   `execve` replaces the image. Its fds: 0 = file `in`, 1 = pipe write end,
   2 = terminal.
10. **Parent**, still in iteration 1, sets `pl.last_pid = pidA` and calls
    `pipeline_parent(A, &pl)`. `prev_read` is -1 so no close. `A->next` is
    non-NULL, so it `close(4)` — this is the write end, and it is the close
    that will eventually let `grep` see EOF — and sets `pl.prev_read = 3`.
11. `cmds = B`.
12. **Iteration 2, cmds = B.** `B->next` is NULL, so **no new pipe** is created;
    `pl.pipefd` still holds the stale values 3 and 4 but nothing reads them
    because every use is guarded by `cmd->next`.
13. `fork()`. Both processes hold fd 3 (the read end).
14. **Child B** enters `pipeline_child`. `prev_read` is 3, so `dup2(3, 0)`
    (stdin is now the pipe) and `close(3)`. `B->next` is NULL, so the pipe
    block is skipped entirely. Child B holds one read end, as fd 0.
    `close_other_heredocs(A, B)` walks the list, skips B, and finds no heredoc
    on A, so again nothing is closed.
15. Child B calls `child_exec`: SIG_DFL restored; `apply_redirs` opens `out`
    with `O_WRONLY|O_CREAT|O_TRUNC` as fd 3, `dup2(3, 1)`, `close(3)`.
    `execve("/usr/bin/grep", {"grep","x"}, envp)`. Its fds: 0 = pipe read end,
    1 = file `out`, 2 = terminal.
16. **Parent** sets `pl.last_pid = pidB` and calls `pipeline_parent(B, &pl)`.
    `prev_read` is 3, so `close(3)` — now the shell holds **no** pipe fd at
    all, and the only surviving ends are one write end in child A and one read
    end in child B. `B->next` is NULL, so `pl.prev_read = -1`.
17. `cmds = NULL`, loop ends.
18. `wait_for_all(pidB)` blocks in `waitpid(-1, ...)`. Meanwhile `cat` writes
    the contents of `in` into the pipe and exits; its fd 1 closes, the write
    reference count drops to zero, `grep` reads EOF, writes its matches to
    `out`, and exits.
19. `wait_for_all` reaps both children in whatever order they finished. It
    records the status only when `pid == pidB`, so `$?` comes from `grep`
    (0 if a line matched, 1 if none did) — not from `cat`.
20. `run_pipeline` returns that status; `exec_line` returns it; `process_line`
    assigns it to `sh->last_status` and calls `free_cmds`, which frees the
    redirection list (and would close any leftover `heredoc_fd`, of which there
    are none here).
21. Back in `shell_loop`, `signals_setup_interactive()` undoes the
    `signals_ignore()` from step 5 before the next prompt.

## Things to be ready to explain

- **Which single `close` prevents the pipeline from hanging?**
  `close(pl->pipefd[1])` in `pipeline_parent`. A pipe reader gets EOF only when
  every write-end reference is closed. The child has its copy as fd 1; if the
  parent kept its copy too, the downstream command would block on `read`
  forever and `cat | grep x` would never return to the prompt.
- **Why does a lone builtin run in the parent but a piped one in a child?**
  So `cd` and `export` can change the shell's own state. In a pipeline bash
  itself discards those effects, because each stage is a separate process, so
  running the builtin in the fork reproduces bash exactly.
- **Why fork every stage before waiting for any of them?**
  Pipeline stages must run concurrently. Waiting for stage 1 first would
  deadlock as soon as its output exceeds the 64 KB pipe buffer — `yes | head -1`
  is the standard demonstration.
- **Why is `signals_ignore()` called in the parent, and what undoes it?**
  While a foreground child runs, ctrl-C belongs to the child, so the shell must
  not die with it. It is undone by `signals_setup_interactive()` at the top of
  the next `shell_loop` iteration, not here.
- **Where does the 130 in `exec_line` come from?**
  `EXIT_SIG_BASE + SIGINT` = 128 + 2. It is returned when `collect_heredocs`
  reports that the user interrupted a heredoc, which is bash's status for an
  abandoned here-document.
- **Is there any fd you do not close?** Not in the normal path any more. The
  one that used to escape was the heredoc read ends belonging to the *other*
  stages of a multi-heredoc pipeline: all heredocs are drained before the first
  fork, and `apply_redirs` consumes only the command's own `heredoc_fd`, so the
  rest survived `execve`. `close_other_heredocs(sh->current_cmds, cmd)` in
  `pipeline_child` now drops them. What remains is the error paths: there is no
  `waitpid` and no fd cleanup on the `pipe()`-failed and `fork()`-failed early
  returns in `run_pipeline`.
- **Why is `close_other_heredocs` called in the child rather than before the
  fork?** Because the parent still needs those fds — each is about to be
  inherited by the stage that actually reads it. Closing them before forking
  would throw away the heredoc bodies. The parent's own copies are released
  later by `free_redirs` when `process_line` frees the command list, so the
  parent was never the leak.
- **How does a child reach the other commands' redirections at all?** Through
  `sh->current_cmds`, set at the top of `exec_line`. `pipeline_child` is handed
  only the single `t_cmd *` it is executing, so the shell struct is its only
  route to the head of the list.
- **Why is `pl.pipefd` safe to read when it was not initialised this
  iteration?** Because every access to it is guarded by `cmd->next`, and
  `cmd->next` is exactly the condition under which `pipe()` was called. The
  last stage never touches the stale values.
