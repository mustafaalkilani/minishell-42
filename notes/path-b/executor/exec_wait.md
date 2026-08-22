# exec_wait.c

Child reaping and status translation. It converts a raw `wait` status word into
the number bash would put in `$?`, prints the newline or `Quit` message the
terminal would otherwise be missing, and reaps every child of the pipeline so no
zombies accumulate. All three functions run in the parent shell only.

## Walkthrough

### `int status_to_exit(int status)`

The `status` handed back by `waitpid` is not an exit code; it is a packed word
that has to be decoded with the `W*` macros. Reading it directly (a common
shortcut is `status / 256`) works by accident on Linux and is not portable, so
the macros are used throughout.

```c
if (WIFSIGNALED(status))
    return (EXIT_SIG_BASE + WTERMSIG(status));
```
The signal case is tested **first**, and the order is not arbitrary: a process
is either signalled or exited, never both, but checking `WIFEXITED` first and
falling through would give a meaningless `WEXITSTATUS` for a killed process.

`EXIT_SIG_BASE` is 128, so ctrl-C on a foreground command (SIGINT, 2) gives
**130** and ctrl-backslash (SIGQUIT, 3) gives **131**. That 128+N convention is
bash's, and it exists so a shell script can distinguish "the program returned 2"
from "the program was killed by signal 2". A program can technically `exit(130)`
itself and be indistinguishable, which is why bash also has `$?` conventions and
nothing more.

```c
if (WIFEXITED(status))
    return (WEXITSTATUS(status));
```
The normal case. `WEXITSTATUS` extracts the low 8 bits the child passed to
`exit`, which is also why `builtin_exit` reduces its argument modulo 256 before
exiting — only 8 bits survive the kernel.

```c
return (EXIT_OK);
```
Fallback for a status that is neither exited nor signalled — in practice
`WIFSTOPPED` or `WIFCONTINUED`. Those can only be reported if `waitpid` was
called with `WUNTRACED` or `WCONTINUED`, and it is not (`wait_for_all` passes
flags of 0), so this branch is unreachable in the current code. It exists so
that the function always returns something defined; returning 0 for an
impossible case is a safe default rather than a claim about semantics.

### `static void report_signal(int status)`

```c
if (!WIFSIGNALED(status))
    return ;
sig = WTERMSIG(status);
```
Only a child that actually died from a signal produces terminal output here; a
child that exited normally prints nothing extra.

```c
if (sig == SIGINT)
    write(STDOUT_FILENO, "\n", 1);
```
When the user presses ctrl-C during `cat`, the terminal echoes `^C` but no
newline, so the next prompt would be printed glued to the `^C`. The shell — not
the child — emits the newline. It cannot come from the SIGINT handler, because
the shell has SIGINT set to `SIG_IGN` for the duration of the pipeline
(`signals_ignore`); the shell learns about the interrupt only afterwards, from
the child's wait status. That is the whole reason this function is driven by
`status` rather than by `g_signal`.

```c
else if (sig == SIGQUIT)
    write(STDOUT_FILENO, "Quit (core dumped)\n", 19);
```
Bash's message for a child killed by SIGQUIT. The length 19 is hard-coded and
must match the literal exactly — count it if asked: `Quit (core dumped)` is 18
characters plus the newline.

Two things to be honest about. First, bash writes these to **stderr**, not
stdout; here they go to fd 1, so if the whole minishell is being piped into a
file by a tester, the newline lands in the diffed stream. Second, the message
claims a core dump unconditionally, whereas bash checks `WCOREDUMP` and prints
plain `Quit` when `ulimit -c` is 0. Neither is a crash or a leak, but both are
divergences you should name rather than defend.

`write` is used rather than `printf` for consistency with the rest of path-b and
because it is a single unbuffered syscall, so the newline cannot end up
reordered behind buffered output.

### `int wait_for_all(pid_t last_pid)`

```c
last_status = EXIT_OK;
pid = waitpid(-1, &status, 0);
while (pid > 0)
{
```
`waitpid(-1, ...)` means "any child", and the loop runs until it returns -1 with
`ECHILD`, i.e. until there are no children left. This is the anti-zombie
guarantee: a pipeline forks N children and this loop reaps all N, whatever order
they finish in. Waiting only for `last_pid` would leave the earlier stages as
zombies for the lifetime of the shell, and a long-running interactive session
would accumulate one entry per pipeline stage in the process table.

The flags argument is 0, so each call blocks. Blocking is correct here: the
shell has nothing else to do while a foreground pipeline runs, and the subject
has no job control, so there is no reason to poll with `WNOHANG`.

```c
    if (pid == last_pid)
    {
        last_status = status_to_exit(status);
        report_signal(status);
    }
```
Every child is reaped, but only the **last** one decides `$?`. That is bash's
pipeline rule without `pipefail`: `false | true` is 0 and `true | false` is 1.
Comparing pids is how the status is filtered, which is why `run_pipeline` keeps
overwriting `pl.last_pid` on each iteration so that it holds the rightmost pid
after the loop.

`report_signal` is likewise called only for the last child, so a pipeline where
an early stage is killed does not print a stray newline.

```c
    pid = waitpid(-1, &status, 0);
}
return (last_status);
```
The loop exits when `waitpid` returns -1. Two ways that happens: `ECHILD`, the
normal termination condition, or `EINTR`. `EINTR` cannot occur here in practice,
because the parent has SIGINT and SIGQUIT set to `SIG_IGN` during the pipeline
and there is no other handler installed — an ignored signal does not interrupt a
syscall. If a handler were ever added without `SA_RESTART`, this loop would exit
early on `EINTR` and leave zombies, so the `SIG_IGN` in `signals_ignore` is
load-bearing for this loop's correctness, not just for the shell's survival.

The initial value `EXIT_OK` is returned in the degenerate case where no child
ever matched `last_pid` — for example if `last_pid` were -1 because no fork
succeeded. `run_pipeline` returns 1 before reaching here on fork failure, so
this is defensive rather than reachable.

## Things to be ready to explain

- **Why 128+N for a signalled child?** It is bash's convention so scripts can
  tell "exited with 2" from "killed by signal 2". `EXIT_SIG_BASE` is 128, so
  SIGINT gives 130 and SIGQUIT gives 131.
- **Why wait for every child but report only the last one's status?** Reaping
  all of them is what prevents zombies; reporting only the last one is bash's
  pipeline semantics — the status of `a | b | c` is the status of `c`.
- **Why does the newline after ctrl-C come from here and not the signal
  handler?** Because the shell has SIGINT ignored while the child runs, so no
  handler fires in the parent at all. The shell only discovers the interrupt by
  decoding the child's wait status, which is exactly what `report_signal` does.
- **Could this loop exit early and leave a zombie?** Only if `waitpid` returned
  -1 with `EINTR`, which needs an installed, non-restarting handler. The parent
  has SIGINT and SIGQUIT set to `SIG_IGN` for the whole pipeline, and ignored
  signals do not interrupt syscalls, so it cannot happen as written.
- **Why the `W*` macros instead of arithmetic on `status`?** The status word is
  an opaque packed value; `status / 256` happens to work on Linux but is not
  portable and gives nonsense for a signalled child. The macros also let us
  distinguish the exited and signalled cases at all.
- **Known divergences?** The `Quit (core dumped)` text and the newline go to
  stdout rather than bash's stderr, and the core-dump wording is unconditional
  rather than gated on `WCOREDUMP`.
