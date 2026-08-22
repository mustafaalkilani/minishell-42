# builtin_env.c

Prints the exported environment, one `KEY=VALUE` per line, in insertion order.
It is deliberately the *unsorted* view of the same `t_env` list that `export`
prints sorted, and it applies the one filter that defines what "exported" means
in this codebase: a node whose `value` is `NULL` — a variable declared with
`export X` and never assigned — is skipped, because such a variable is not passed
to child processes. The subject allows `env` with no options and no arguments,
so anything else is refused rather than emulated.

## Walkthrough

### `int builtin_env(char **argv, t_shell *sh)`

```c
if (argv[1])
{
    shell_error("env", argv[1], "No such file or directory");
    return (EXIT_NOT_FOUND);
}
```
Any argument at all is rejected. The reason this specific message and this
specific status were chosen is bash parity: the real `/usr/bin/env` treats its
first non-option operand as a program to execute, so `env foo` prints
`env: 'foo': No such file or directory` and exits **127**. `EXIT_NOT_FOUND` is
127 in `includes/minishell.h`, so `env nosuchthing` here produces
`minishell: env: nosuchthing: No such file or directory` and `$?` is 127, which
is what a tester comparing against bash will see.

The honest caveat: this branch also catches genuine *options*. `env -i` and
`env -u X` are valid for the real `env`, and here they produce the same
"No such file or directory" message with 127. The subject explicitly scopes `env`
to "no options, no arguments", so implementing `env VAR=x cmd` was never in
range, and refusing everything uniformly is the simplest way to avoid pretending
otherwise.

Nothing has been allocated at this point, so the early return leaks nothing.

```c
cur = sh->env;
while (cur)
{
    ...
    cur = cur->next;
}
```
A plain walk of the singly linked list from the head. The order printed is
therefore **insertion order**: whatever `environ` gave us at startup, in the
order `env_init` consumed it, with anything created later by `export` appended at
the back by `env_add_back`. This is intentional and is the visible difference
from `export`, which sorts a temporary array of pointers instead of touching the
list (see `builtin_export_list.c`). Bash likewise prints `env` unsorted and
`export` sorted.

```c
if (cur->value)
{
    ft_putstr_fd(cur->key, STDOUT_FILENO);
    ft_putchar_fd('=', STDOUT_FILENO);
    ft_putendl_fd(cur->value, STDOUT_FILENO);
}
```
The `if (cur->value)` is the whole semantic content of the function. `value ==
NULL` is this codebase's representation of "declared but unset", produced by
`env_set_from_string` when the argument to `export` contained no `=`. Such a
variable must not appear in `env`, because it is not in the environment block
that `execve` receives — and indeed `env_export_count`/`env_to_envp` in
`path-b/env/env_export.c` apply exactly the same `if (env->value)` filter. Keeping
the two in agreement is the invariant: what `env` prints is precisely what a
child process would receive.

The distinction is visible in three steps: `export X` then `env | grep X` prints
nothing, but `export | grep X` prints `declare -x X`, and `export X=` then
`env | grep X` prints `X=` (empty value, but a non-NULL pointer to an empty
string, so it is exported).

Output is built from three separate write calls rather than a joined string, so
the function performs **no allocation at all** and consequently has no error path
and nothing to free. `ft_putendl_fd` supplies the trailing newline.

Everything goes to `STDOUT_FILENO`, which is why `env > file` and `env | sort`
work; the redirection itself was applied by `run_parent_builtin` or
`apply_redirs` before this function was entered.

```c
return (EXIT_OK);
```
0. The only other status this function can produce is 127 from the argument
check; there is no failure mode in the printing loop.

## Things to be ready to explain

- **Why does `export X` not show up in `env`?** Because `X` is stored as a node
  with `value == NULL`, meaning "declared but never assigned", and the loop skips
  those. That mirrors bash: such a variable is listed by `export` but is not part
  of the environment handed to children. `env_to_envp` applies the same filter,
  so `env`'s output and a child's environment always agree.
- **What is the difference between `export X` and `export X=`?** `export X`
  gives `value == NULL` (not exported, no `=` shown by `export`). `export X=`
  gives `value` pointing at an empty string, so `env` prints `X=` and a child
  really does receive `X=`. One test distinguishes them; evaluators like it.
- **Why 127 for `env foo` instead of 1 or 2?** The real `env` tries to execute
  its operand as a command, and "command not found" is 127. Copying the message
  and the status keeps `$?` identical to bash for the case testers actually run.
- **Why is `env` unsorted when `export` is sorted?** They read the same list, but
  `export_list` copies the node pointers into an array and bubble-sorts that
  array, leaving the list itself in insertion order. Sorting the list in place
  would break `env`'s ordering, which bash keeps as the inherited order.
- **Does `env` allocate anything?** No. It writes the key, `=` and the value with
  three separate `fd` writes, so there is no buffer to free and no
  allocation-failure path.
- **What happens to `env` inside a pipeline?** It runs in the forked child via
  `child_exec`, reading the copy of the `t_env` list that the child inherited. It
  prints the same thing, and since it mutates nothing there is no visible
  difference from running it alone — unlike `export` or `unset`.
