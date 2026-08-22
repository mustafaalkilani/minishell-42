# builtin_dispatch.c

The lookup table for the seven mandatory builtins, expressed as two functions
instead of a table because Norminette makes a static array of function pointers
with mismatched signatures awkward. `is_builtin` answers "should this name be
handled internally instead of being resolved on PATH", and `run_builtin` routes
an already-identified builtin to its implementation and returns its exit status.
Nothing here forks, redirects or frees; the caller (`exec_line` for a lone
builtin, `child_exec` for one inside a pipeline) has already decided which
process this runs in.

## Walkthrough

### `int is_builtin(const char *name)`

```c
if (!name)
    return (0);
```
Defensive NULL guard. In practice `exec_line` already checks `cmds->argv[0]`
before calling, and `child_exec` checks `!cmd->argv[0]` and exits first, so this
branch should be unreachable. It costs one comparison and removes any doubt
about a command whose argv expanded to nothing (`$EMPTY` on a line by itself),
where `argv[0]` is `NULL`.

```c
if (!ft_strncmp(name, "echo", 5) || !ft_strncmp(name, "cd", 3))
    return (1);
if (!ft_strncmp(name, "pwd", 4) || !ft_strncmp(name, "env", 4))
    return (1);
if (!ft_strncmp(name, "export", 7) || !ft_strncmp(name, "unset", 6))
    return (1);
if (!ft_strncmp(name, "exit", 5))
    return (1);
return (0);
```
Each length is `strlen + 1`, which is the idiom that turns `strncmp` into an
exact-string comparison: the extra byte forces the terminating `'\0'` to be part
of the comparison, so `echon` does not match `echo` and `cd` does not match
`cdrom`. Using `ft_strncmp(name, "echo", 4)` would be the classic bug — `echoes`
would then be treated as a builtin and the real `/bin/echoes` would never be
searched for. Verify the numbers when defending: echo/exit 5, cd 3, pwd/env 4,
export 7, unset 6.

The comparison is case-sensitive because bash has no builtin `ECHO`. Typing
`ECHO hi` in bash produces "command not found" (or runs a program of that name),
and returning 0 here reproduces that: the name falls through to
`resolve_command` and the PATH search.

The tests are grouped two-per-`if` purely to stay inside Norminette's 25-line
function limit; there is no ordering significance, since the names are disjoint.

### `int run_builtin(t_cmd *cmd, t_shell *sh)`

```c
name = cmd->argv[0];
```
No NULL check here. That is intentional but load-bearing: `run_builtin` is only
ever reached after the caller has confirmed `is_builtin(cmd->argv[0])`, which
already required a non-NULL `argv[0]`. Both call sites (`run_parent_builtin` via
`exec_line`, and `child_exec`) satisfy that.

```c
if (!ft_strncmp(name, "echo", 5))
    return (builtin_echo(cmd->argv));
if (!ft_strncmp(name, "cd", 3))
    return (builtin_cd(cmd->argv, sh));
if (!ft_strncmp(name, "pwd", 4))
    return (builtin_pwd());
if (!ft_strncmp(name, "env", 4))
    return (builtin_env(cmd->argv, sh));
if (!ft_strncmp(name, "export", 7))
    return (builtin_export(cmd->argv, sh));
if (!ft_strncmp(name, "unset", 6))
    return (builtin_unset(cmd->argv, sh));
return (builtin_exit(cmd->argv, sh));
```
The whole `argv` is handed down, not `argv + 1`, so every builtin starts its own
argument scan at index 1. That keeps `argv[0]` available for error messages of
the shape `minishell: export: ...` and means the builtins can be reasoned about
as if they were `main`.

Which builtins receive `sh` is exactly the set that touches shell state: `cd`
(reads HOME/OLDPWD, writes PWD/OLDPWD), `env`, `export`, `unset` (the env list),
and `exit` (`last_status` for `exit` with no argument, plus `current_line`,
`current_cmds` and `env` for the cleanup before `exit()`). `echo` and `pwd` are pure and take no
shell, which is a small guarantee that they cannot corrupt anything.

The last line is a **fallthrough default**, not a test. Because six names have
been excluded above and `is_builtin` guaranteed the name is one of the seven,
whatever remains must be `exit`. This is safe today but fragile: if anyone ever
calls `run_builtin` without first calling `is_builtin`, an unknown command name
would terminate the shell instead of being reported. Both existing call sites
guard correctly. The reason it is written this way is Norminette's line budget —
an eighth `if` plus a final `return (EXIT_OK)` would not change behaviour but
would push the function toward the limit.

Note that `run_builtin` returns an `int` status for every builtin except `exit`,
which normally never returns at all: `builtin_exit` calls `exit()` directly.
The single case where it does return is `exit 1 2` (too many arguments), which
returns 1 and leaves the shell alive.

## Things to be ready to explain

- **Why `ft_strncmp(name, "echo", 5)` and not `4`?** The `+1` includes the NUL
  terminator in the comparison, turning a prefix test into an exact-match test.
  With `4`, `echoxyz` would be dispatched to `builtin_echo` and the real program
  on PATH would be shadowed.
- **What happens if `run_builtin` is given a name that is not a builtin?** It
  falls through to `builtin_exit` and would terminate the shell. That cannot
  happen from the current code because both callers test `is_builtin` first, but
  it is a real precondition of the function and worth stating plainly rather
  than claiming the code is defensive here.
- **Why do only some builtins take `t_shell *sh`?** Only the ones that read or
  mutate shell state need it: `cd`, `env`, `export`, `unset`, `exit`. `echo` and
  `pwd` are self-contained, so they cannot accidentally touch the environment.
- **Does this file decide whether the builtin runs in the parent or a child?**
  No. That decision is in `exec_line`: a builtin alone on the line goes through
  `run_parent_builtin` (so `cd`/`export`/`unset` persist), while a builtin inside
  a pipeline is reached from `child_exec` after a fork, so its effects die with
  the child — which is exactly what bash does.
- **Why is the matching case-sensitive?** Bash's builtin table is
  case-sensitive; `ECHO` is not a builtin there. Returning 0 sends the name to
  the PATH search, reproducing bash's "command not found" (status 127).
