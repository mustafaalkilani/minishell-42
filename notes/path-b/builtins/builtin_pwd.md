# builtin_pwd.c

The smallest builtin in the project: print the current working directory and
return 0. It asks the kernel with `getcwd` rather than echoing `$PWD`, which is
the one design decision in the file and the one an evaluator will ask about. It
takes no shell state and no arguments, so it cannot fail for any reason other
than the directory itself having become unavailable.

## Walkthrough

### `int builtin_pwd(void)`

```c
cwd = getcwd(NULL, 0);
```
The GNU form of `getcwd`: passing a `NULL` buffer and size 0 makes libc allocate
a buffer of exactly the right length with `malloc`. The alternative — a fixed
`char buf[PATH_MAX]` — would silently truncate on a deeply nested path and would
put 4 KB on the stack. The returned pointer is **ours to free**, and this
function is the sole owner of it for its whole lifetime.

```c
if (!cwd)
{
    shell_error("pwd", NULL, strerror(errno));
    return (1);
}
```
`getcwd` really can fail. The realistic case is that the directory the shell is
sitting in was removed or renamed by another process, which gives `ENOENT`, or
that a parent component became unsearchable, which gives `EACCES`. On failure
`getcwd` returns `NULL` and has allocated nothing, so there is nothing to free
on this path — the early `return` is leak-free.

`strerror(errno)` reproduces the system's own wording, which is how the message
ends up looking like bash's. Bash's `pwd` also returns **1** on failure, so the
status matches. There is no `EXIT_MISUSE` path in this file at all.

```c
ft_putendl_fd(cwd, STDOUT_FILENO);
free(cwd);
return (EXIT_OK);
```
Written to `STDOUT_FILENO` because the path is data, not a diagnostic — that is
what makes `pwd > file` and `pwd | cat` work. Any redirection was already applied
by `run_parent_builtin` (which dup'd the target onto fd 1 and will restore it
afterwards) or by `apply_redirs` inside the forked child, so this builtin never
touches a descriptor other than the constant 1.

`free(cwd)` immediately after printing is the entire memory story of the file:
one allocation, one free, no ownership handed anywhere. `EXIT_OK` is 0 from
`includes/minishell.h`.

Note what is deliberately absent: no argument handling. `builtin_pwd` is declared
`void`, and `run_builtin` calls it as `builtin_pwd()` without passing `argv` at
all, so `pwd -P`, `pwd foo` and `pwd --nonsense` all just print the directory and
return 0. Bash accepts `-L`/`-P` and rejects other options with status 2. Neither
option is in the mandatory subject, and silently ignoring extra arguments is the
common choice; be ready to name it as a known simplification rather than claim it
is bash-identical.

## Things to be ready to explain

- **Why `getcwd` instead of printing `$PWD`?** `PWD` is an ordinary environment
  variable and the user can lie about it: `export PWD=/nowhere` would then make
  `pwd` print `/nowhere`. `getcwd` asks the kernel for the real path, so `pwd`
  stays truthful. Bash's builtin `pwd` (without `-L`) behaves the same way in
  practice for this project's purposes.
- **Why `getcwd(NULL, 0)` and who frees the result?** The `NULL`/0 form makes
  libc malloc a correctly sized buffer, avoiding `PATH_MAX` truncation. This
  function owns the buffer and frees it right after printing; on the failure path
  `getcwd` returned `NULL` and allocated nothing, so there is nothing to free.
- **When can `pwd` fail, and what does it return?** When the current directory
  has been deleted or a parent component became unsearchable — `getcwd` returns
  `NULL` with `ENOENT` or `EACCES`. The builtin prints
  `minishell: pwd: <strerror>` on stderr and returns 1, which is bash's status.
  Otherwise it returns `EXIT_OK`, 0.
- **Why does `builtin_pwd` take no arguments at all?** It needs neither `argv`
  (no options are supported) nor `t_shell` (it reads no shell state). The
  signature `int builtin_pwd(void)` documents that it is a pure operation that
  cannot corrupt the environment.
- **What does `pwd foo` do here versus in bash?** Here it ignores the extra
  argument and prints the directory with status 0, because `argv` is never
  inspected. Bash also ignores non-option operands, but it does reject unknown
  options such as `pwd -x` with status 2. That option handling is not
  implemented, and it is outside the mandatory part.
- **How does `pwd > out` work if this file never opens a file?** The redirection
  is applied before the builtin runs — by `run_parent_builtin` for a lone builtin
  (which saves and restores fd 0 and 1 around the call) or by `apply_redirs` in
  the forked child inside a pipeline. `pwd` only ever writes to the constant
  `STDOUT_FILENO`.
