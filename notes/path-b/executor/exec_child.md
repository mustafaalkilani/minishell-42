# exec_child.c

Everything that happens inside a forked child between `fork()` returning 0 and
the process image being replaced. It restores the default signal dispositions,
applies the command's redirections, dispatches builtins that happen to be
inside a pipeline, resolves the program on PATH, and — when `execve` fails —
translates `errno` into the exit code bash would produce. No function here ever
returns to its caller: every path ends in `exit()` or a successful `execve`.

## Walkthrough

### `static void execve_failed(char *cmd)`

Reached only when `execve` returned, which it does only on failure. The whole
job of this function is picking between 126 and 127, and getting that wrong is
one of the most commonly caught bugs at defence.

```c
struct stat st;

if (errno == EISDIR
    || (errno == EACCES && stat(cmd, &st) == 0 && S_ISDIR(st.st_mode)))
{
    shell_error(cmd, NULL, "Is a directory");
    exit(EXIT_CANT_EXEC);
}
```
Trying to execute a directory is reported differently by different kernels.
POSIX allows `EISDIR`, but Linux returns `EACCES` for `execve` on a directory,
so testing `errno` alone is not enough — hence the extra `stat` and
`S_ISDIR` check behind the `EACCES` arm. The `&&` short-circuit matters: `stat`
is only called when `errno` is already `EACCES`, so we do not pay for a syscall
on the common not-found path, and `st` is only read after `stat` returned 0.

`EXIT_CANT_EXEC` is 126: "the file was found but could not be executed". Running
`/tmp` or `./somedir` in bash prints `Is a directory` and gives 126, and this
reproduces it exactly.

One honest caveat about the argument: `execve_failed` is called with
`cmd->argv[0]`, the name the user typed, not with the resolved `path`. For a
PATH-searched command those differ (`ls` versus `/usr/bin/ls`), so the `stat`
would be resolving `ls` relative to the current directory. In practice this
never misfires, because `resolve_command`'s PATH search already rejects
directories via `is_executable_file`, so a PATH-resolved name can never reach
here as a directory. The directory case only arises when the name contains a
`/`, and then `argv[0]` and `path` are the same string. It is correct, but it is
correct by construction rather than obviously — say so rather than claiming the
argument was chosen for that reason.

```c
if (errno == EACCES)
{
    shell_error(cmd, NULL, "Permission denied");
    exit(EXIT_CANT_EXEC);
}
```
A plain, non-directory `EACCES`: the file exists but the execute bit is not set
for us. Again 126, again matching bash's `Permission denied` wording.

```c
shell_error(cmd, NULL, strerror(errno));
exit(EXIT_NOT_FOUND);
```
Everything else — `ENOENT` (no such file), `ENOTDIR` (a path component is not a
directory), `ENOEXEC` (bad binary format) — is reported with the system's own
message and exits 127. `EXIT_NOT_FOUND` is 127, bash's "command not found"
status. Using `strerror(errno)` rather than a hard-coded string is what makes
`./nope` print `No such file or directory` and `/etc/passwd/x` print
`Not a directory`, exactly as bash does.

Note this is a child process, so `exit()` here terminates only the child; the
shell is untouched and later collects this number through `waitpid`.

### `void child_exec(t_cmd *cmd, t_shell *sh)`

The declared return type is `void` and there is no `return` statement, because
the function genuinely cannot come back — that property is what lets
`run_pipeline` write `if (pid == 0) pipeline_child(...)` without an `else`.

```c
signals_setup_child();
```
First thing, before anything can block. The parent called `signals_ignore()`
before forking, so this child inherited `SIG_IGN` for SIGINT and SIGQUIT.
Ignored dispositions **survive `execve`** (only installed handlers are reset to
default across an exec), so without this call `cat` with no arguments would be
completely immune to ctrl-C and the shell would be stuck. Resetting to `SIG_DFL`
here is what makes the child die on ctrl-C and, in turn, what lets
`status_to_exit` report 130.

It is done before `apply_redirs` so that a child which blocks opening a FIFO,
or a builtin that loops, is already interruptible.

```c
if (apply_redirs(cmd) < 0)
    exit(1);
```
Redirections are applied inside the child, after the pipe dups done by
`pipeline_child`. Order is deliberate: `ls > out | cat` must send `ls`'s output
to the file, not to the pipe, and since `apply_redirs` runs second its `dup2`
onto fd 1 overwrites the pipe. That is bash's precedence too.

A failed redirection exits **1**, not 126 or 127, because the command was never
even attempted; bash also reports 1 for `echo hi > /nonexistent/dir/f`. The
error message itself was already printed by `apply_one`.

Because this exit happens in a child, no fd cleanup is needed: process death
closes the pipe ends, and the parent's `wait_for_all` still reaps it.

```c
if (!cmd->argv[0])
    exit(EXIT_OK);
```
A command with redirections but no words — `> out` on its own, or a line that
expanded to nothing but still had a redirect. The redirection above already
created or truncated the file, which is the entire observable effect, so we exit
0. `argv` itself is never `NULL` (the parser always allocates a one-element
NULL-terminated array), so this dereference is safe.

```c
if (is_builtin(cmd->argv[0]))
    exit(run_builtin(cmd, sh));
```
Any builtin reaching this point is inside a pipeline, because `exec_line`
diverted the lone-builtin case to `run_parent_builtin` in the parent. Running it
here means its side effects — a changed `cwd` from `cd`, a new variable from
`export` — die with the child, which is precisely what bash does: `export A=1 |
cat` leaves `A` unset in the parent shell.

The builtin's return value becomes the child's exit code directly, so
`echo hi | cat` still ends with `$?` from `cat`, and `false-ish` builtins in a
pipeline report their own status through `waitpid`.

Note `exit()` here flushes nothing of ours (we write with `write`, not `stdio`),
but it does run `atexit` handlers and does *not* free the shell's env list. That
is an accepted small leak in a process that is about to die; it is invisible to
valgrind's default child-follow settings and the subject does not require
leak-freeing a doomed child.

```c
path = resolve_command(cmd->argv[0], sh);
if (!path)
{
    shell_error(cmd->argv[0], NULL, "command not found");
    exit(EXIT_NOT_FOUND);
}
```
`resolve_command` returns `NULL` in three situations: an empty command name, a
PATH search that found nothing executable, or an allocation failure. All three
are reported as `command not found` with status **127**, which is bash's code
for a name that could not be resolved.

Notice this is where `.` and `..` land. Neither contains a `/`, so both go
through the PATH search, and every candidate such as `/usr/bin/.` is rejected by
`is_executable_file` because it is a directory. Without that rejection
`access(X_OK)` would have accepted `/usr/bin/.`, `execve` would have been called
on a directory, and the code would have come out as 126 instead of 127.

```c
envp = env_to_envp(sh->env);
execve(path, cmd->argv, envp);
```
The environment is flattened from the `t_env` linked list into the
`char **` array `execve` demands, and only here — not on every lookup — because
this is the one place that needs the flat form. If `execve` succeeds this
process's entire address space is replaced, so `path` and `envp` are not leaked;
they simply cease to exist along with everything else. Open file descriptors,
by contrast, do survive, which is why all the closing had to happen earlier.

`cmd->argv` is passed straight through, so `argv[0]` inside the new program is
the name the user typed (`ls`), not the resolved path (`/usr/bin/ls`) — matching
bash, and visible in things like `ps`.

```c
free(path);
free_split(envp);
execve_failed(cmd->argv[0]);
```
Only reachable when `execve` failed. Both allocations are released first so that
a valgrind run following children does not report them, and *then*
`execve_failed` inspects `errno` and exits. The ordering is safe because
`free` does not touch `errno` on any implementation we rely on — though strictly
speaking the C standard permits library functions to clobber `errno`, so this is
a very small assumption. If an evaluator raises it, the robust version would
save `errno` into a local before the frees.

## Things to be ready to explain

- **Why 126 versus 127?** 127 means the name could not be resolved at all
  (`resolve_command` returned NULL, or `execve` failed with `ENOENT`). 126 means
  the file was found but is not runnable: it is a directory, or the execute bit
  is missing. Both come straight from bash's convention.
- **Why check `S_ISDIR` after `EACCES` instead of trusting `errno`?** Linux
  reports `execve` on a directory as `EACCES`, not `EISDIR`. Without the extra
  `stat` we would print `Permission denied` for `/tmp` instead of
  `Is a directory`. Both give 126, so only the message differs — but the message
  is diffed against bash.
- **Why must the child reset signals to `SIG_DFL`?** The parent set them to
  `SIG_IGN` before forking, and an ignored disposition survives `execve`. Without
  the reset, `cat` would be unkillable with ctrl-C. Installed handlers, unlike
  `SIG_IGN`, are reset automatically by `execve`, which is why the parent's
  interactive handler is not a problem here.
- **Why is a builtin executed here at all?** Because it is in a pipeline.
  `exec_line` sends a *lone* builtin to the parent so `cd`/`export` persist; a
  piped one belongs in the child so its effects are correctly discarded, exactly
  as in bash.
- **Do you leak `path` and `envp`?** Not on the success path — `execve` destroys
  the whole address space. On the failure path they are explicitly freed before
  `execve_failed` exits.
- **Why does a failed redirection exit 1 rather than 126?** Because no program
  was ever run. 126 is reserved for "found the command but could not execute
  it"; a bad `> /nope/f` is a redirection error, which bash also reports as 1.
