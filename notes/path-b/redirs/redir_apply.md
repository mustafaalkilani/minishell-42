# redir_apply.c

Applies a command's `t_redir` list to the current process's file descriptors.
It opens the target files with the right flags, duplicates them onto fd 0 or
fd 1, and closes every temporary descriptor it creates. It is called from two
very different places — inside a forked child (`child_exec`) and inside the
shell itself (`run_parent_builtin`) — and the difference in who has to undo the
damage afterwards is the thing to understand.

The file also owns the other half of heredoc fd ownership: `close_other_heredocs`
lets a pipeline child drop the heredoc read ends that belong to the *other*
stages, which it inherited only because every heredoc on the line is drained
before the first fork.

At five functions the file is exactly at norminette's per-file limit, so
anything further that redirection handling needs has to go into a new `.c`.

## Walkthrough

### `static int open_target(t_redir *redir)`

```c
if (redir->type == R_IN)
    return (open(redir->filename, O_RDONLY));
```
`< file` opens read-only and, crucially, without `O_CREAT`: reading from a file
that does not exist is an error, not an invitation to create an empty one. The
missing-file case comes back as -1 with `ENOENT` and is reported by the caller.

```c
if (redir->type == R_APPEND)
    return (open(redir->filename, O_WRONLY | O_CREAT | O_APPEND, 0644));
return (open(redir->filename, O_WRONLY | O_CREAT | O_TRUNC, 0644));
```
The two output forms differ in exactly one flag. `O_TRUNC` for `>` empties an
existing file at open time — which is why `> file` alone, with no command, is a
valid way to blank a file, and why the truncation happens even if the command
then fails to run. `O_APPEND` for `>>` makes every write seek to the end
atomically, so two processes appending to the same file interleave lines rather
than overwriting each other.

`0644` is the mode used only when `O_CREAT` actually creates the file. It is the
raw request; the process umask (typically 022) is subtracted, so the file
usually ends up 0644 anyway on a default system. Bash uses 0666 and lets the
umask reduce it to 0644; requesting 0644 directly means a user with umask 0 gets
0644 from us and 0666 from bash. That is a cosmetic divergence worth knowing
about rather than defending as intentional.

The final `return` handles `R_OUT` by falling through rather than testing for
it. That keeps the function inside Norminette's line budget, and it is safe
because the parser only ever produces the four `t_redir_type` values and
`R_HEREDOC` is intercepted before this function is called.

### `static int apply_one(t_redir *redir)`

```c
if (redir->type == R_HEREDOC)
{
    if (redir->heredoc_fd < 0)
        return (-1);
```
A heredoc has no file to open. Its body was already drained into a pipe by
`collect_heredocs` before any fork happened, and the pipe's **read** end is
sitting in `redir->heredoc_fd`. A value below zero means the heredoc was never
collected or was already consumed, which is treated as a redirection failure.

```c
    dup2(redir->heredoc_fd, STDIN_FILENO);
    close(redir->heredoc_fd);
    redir->heredoc_fd = -1;
    return (0);
}
```
`dup2` makes fd 0 a second reference to the pipe read end, then the original is
closed because two names for the same thing are one too many. Since the write
end was already closed inside `read_heredoc`, the command reads the buffered
body and then immediately sees EOF — no deadlock is possible, and nothing has to
be written concurrently.

Setting `heredoc_fd = -1` after the close is what makes this idempotent and, more
importantly, what keeps `free_redirs` (in `path-a/parser/parser_utils.c`) from
closing an already-closed descriptor: that function closes any `heredoc_fd >= 0`
when the command list is freed. The sentinel is the handshake between the two.

Note the consequence when this runs in a **child**: the child's copy of the fd
is closed, but the parent still holds its own copy of the same read end until
`free_cmds` runs after the pipeline finishes. That is harmless (a spare read end
cannot prevent EOF) and it is how the parent's descriptor eventually gets closed
at all.

Also note what does **not** happen here: heredoc fds belonging to *other*
commands in the same pipeline are untouched. This function only ever sees one
`t_redir` at a time, and `apply_redirs` only ever walks one command's list, so
a child in `cat << A | cat << B` would still carry the other stage's read end
across `execve`. That is what `close_other_heredocs` below exists for, and it
is called from `pipeline_child` before `child_exec` ever reaches this code.

```c
fd = open_target(redir);
if (fd < 0)
{
    shell_error(redir->filename, NULL, strerror(errno));
    return (-1);
}
```
The message is built from `errno`, so `< nope` prints `No such file or
directory` and `> /root/x` prints `Permission denied`, matching bash's wording
per case rather than a single generic string. `shell_error` prefixes
`minishell: ` and the filename.

Returning -1 propagates up: in a child that becomes `exit(1)`, in the parent
builtin path it becomes a status of 1 with the builtin never run. Both are
bash's behaviour.

```c
if (redir->type == R_IN)
    dup2(fd, STDIN_FILENO);
else
    dup2(fd, STDOUT_FILENO);
close(fd);
return (0);
```
`open` returned the lowest available descriptor — typically 3 in a child that
has already done its pipe dups. `dup2` copies it onto 0 or 1, **implicitly
closing whatever was there first**. That implicit close is how a redirection
overrides a pipe: in `ls > out | cat`, `pipeline_child` put the pipe write end on
fd 1, and this `dup2` replaces it, so `ls` writes to the file and `cat` sees an
immediately-closed pipe. It is also the correct precedence — bash does the same.

`close(fd)` is mandatory. Without it the process holds two references to the
file: fd 1 and fd 3. In a child that is about to `execve`, fd 3 would survive
into the new program's descriptor table, which is both a leak and observable
(`ls -l /proc/self/fd`). In the **parent builtin** case it is worse: `run_parent_
builtin` restores fd 0 and fd 1 from its backups but knows nothing about fd 3, so
every `pwd > f` would strand one descriptor permanently and the shell would run
out after a few thousand redirections.

There is no `dup2` return check. A failure here would need an invalid target fd
or the fd limit, and neither is reachable given that `fd` was just successfully
opened; still, it is an unchecked call, so say so rather than claim it cannot
fail.

### `static void close_heredoc_list(t_redir *redirs)`

```c
while (redirs)
{
    if (redirs->type == R_HEREDOC && redirs->heredoc_fd >= 0)
    {
        close(redirs->heredoc_fd);
        redirs->heredoc_fd = -1;
    }
    redirs = redirs->next;
}
```
One command's redirection list, closing every collected heredoc read end. The
two conditions are both needed: `type == R_HEREDOC` because `heredoc_fd` is
meaningless for the other three types, and `>= 0` because the field is `-1`
both before collection and after `apply_one` has consumed it, and closing `-1`
would be an `EBADF` we would not notice.

The reset to `-1` is the same ownership handshake `apply_one` performs, for the
same reason: `free_redirs` in `path-a/parser/parser_utils.c` closes any
`heredoc_fd >= 0` when the command list is freed. Without the reset, a child
that closed an fd here and then somehow reached a free path would double-close
it. In practice the child execs and never frees, but the sentinel is what makes
the invariant "a non-negative `heredoc_fd` is an fd this process still owns"
true everywhere rather than only on the paths we happen to take.

Being `static` is deliberate: nothing outside this file has a reason to close
one command's heredocs in isolation.

### `void close_other_heredocs(t_cmd *cmds, t_cmd *self)`

```c
while (cmds)
{
    if (cmds != self)
        close_heredoc_list(cmds->redirs);
    cmds = cmds->next;
}
```
The public half, and the reason the helper above exists. `collect_heredocs`
drains **every** heredoc on the line into a pipe before the first fork, because
heredoc bodies are typed interactively and only the shell process can read
them. The price of that timing is that when `run_pipeline` later forks one
child per stage, each child inherits the read ends belonging to every other
stage as well as its own.

`apply_redirs` consumes and closes only the fd belonging to its own command, so
without this loop the rest survive `execve` and show up as open descriptors in
the executed program. The `cmds != self` test is the whole logic: skip my own
command — its heredoc is still needed, `apply_one` is about to `dup2` it onto
stdin — and close everybody else's.

Pointer identity is the right comparison here because the child was handed the
very `t_cmd *` it is executing, and the node it points at is one of the nodes in
the list being walked. No index or name matching is involved.

Reproduced before this existed, with a two-heredoc pipeline:

```
$ printf 'cat << A | cat << B | ls /proc/self/fd\nx\nA\ny\nB\n' | ./minishell
0
1
2
3
4      <-- leaked
5      <-- leaked
```
bash prints only `0` through `3`. With the call in place minishell prints only
`0` through `3` too.

Two things this is *not*. It is not a deadlock fix — every heredoc pipe's write
end was already closed inside `read_heredoc`, so the extra read ends could never
have held a writer open. And it is not a parent-side fix: the shell process was
never at fault, because `free_redirs` closes any remaining `heredoc_fd` when the
command list is freed at the end of `process_line`. The leak was only ever in
the exec'd program's descriptor table.

### `int apply_redirs(t_cmd *cmd)`

```c
cur = cmd->redirs;
while (cur)
{
    if (apply_one(cur) < 0)
        return (-1);
    cur = cur->next;
}
return (0);
```
Walks the list **in the order the parser built it**, which is left-to-right
source order. That ordering is the semantics: `> a > b` opens and truncates `a`,
dups it onto fd 1, closes the temp, then opens `b` and dups it onto fd 1 — the
second `dup2` silently closing the first. The net effect is that both files are
created, `a` is left empty, and output goes to `b`. Bash does exactly this.
Likewise `< a < b` reads from `b` but still errors if `a` does not exist.

The early `return (-1)` stops at the first failure, leaving any earlier
redirections already applied. That is not a leak — each `apply_one` closed its
own temporary fd before returning — but it does mean fd 0 or fd 1 may already
have been reassigned when the failure is reported. In a child that does not
matter (it exits immediately). In `run_parent_builtin` it is handled by the
`saved_in`/`saved_out` restore, which runs on the failure path too. This is why
that save/restore is unconditional rather than only on success.

## Things to be ready to explain

- **Why `close(fd)` after every `dup2`?** Because `dup2` creates a second
  reference, and leaving the original open leaks it. In a child it survives
  `execve` into the exec'd program; in the parent builtin path nothing ever
  closes it, so repeated redirections would exhaust the fd table.
- **Why does `> a > b` still create `a`?** Redirections are applied left to
  right and each one is really performed. `a` is opened with `O_TRUNC` and dup'd
  onto fd 1; then `b` is opened and its `dup2` displaces it. Both files exist,
  `a` is empty, output lands in `b` — same as bash.
- **Why does a heredoc need no `open`?** Its body was already read into a pipe
  before the fork, by `collect_heredocs`. Only the read end is stored, and the
  write end was closed at collection time, so the command reads the body and
  then gets EOF with no second process involved.
- **Why set `heredoc_fd = -1` after closing it?** So `free_redirs` — which
  closes any `heredoc_fd >= 0` when the command list is freed — does not close
  an already-closed descriptor. The sentinel is the ownership handshake between
  the applier and the deallocator.
- **What happens to a pipe when a redirection targets the same fd?** The
  redirection wins, because `dup2` closes the target first. `ls > out | cat`
  writes to `out` and gives `cat` an empty pipe, which is bash's precedence.
- **Does a failed redirection leave fds in a broken state?** In a child, yes,
  but the child exits with 1 immediately. In the parent, `run_parent_builtin`
  restores fd 0 and fd 1 from `dup` backups on both the success and failure
  paths, which is why that save/restore is unconditional.
- **Why does `close_other_heredocs` exist at all?** Because heredocs must be
  drained before the first fork — they are read interactively — so every child
  inherits every stage's heredoc read end. `apply_redirs` closes only its own,
  so the others would survive `execve`. `ls /proc/self/fd` at the end of
  `cat << A | cat << B | ...` showed them before the fix.
- **Why is it called in the child and not in the parent?** The parent needs
  those fds: each is about to be inherited by the child that actually wants it,
  and the parent's own copies are closed by `free_redirs` when the command list
  is freed. Closing them in the parent before forking would destroy the data.
- **Why compare with `!=` on the pointer instead of a name or index?** The child
  is handed the exact `t_cmd *` it is executing, and that node is one of the
  nodes in the list being walked, so identity is exact. Two stages can easily be
  the same command (`cat << A | cat << B`), which is precisely the case a
  name-based test would get wrong.
- **Could the leaked fds have caused a hang?** No. Every heredoc pipe's write
  end is closed inside `read_heredoc` at collection time, so a spare read end
  can never keep a writer alive. It was a descriptor-table leak, observable but
  not fatal.
- **Why is the file at its size limit now?** Five functions is norminette's
  maximum per `.c`, and `redir_apply.c` has exactly five: `open_target`,
  `apply_one`, `close_heredoc_list`, `close_other_heredocs`, `apply_redirs`.
  Any further redirection helper needs a new file in `path-b/redirs/`.
