# builtin_export.c

Adds or updates variables in the shell's `t_env` list, and with no arguments
delegates to `export_list` to print the sorted `declare -x` listing. The two
behaviours worth defending are the two different failure modes: a *leading*
argument that starts with `-` is an option, which aborts the whole builtin with
status 2, while an argument that is merely not a valid identifier is reported and
then **skipped**, letting the remaining arguments still be processed, with an
overall status of 1. Both were checked against bash 5.2.21.

## Walkthrough

### `static int invalid_option(const char *arg)`

```c
if (arg[0] != '-' || !arg[1])
    return (0);
```
Two conditions to be an option: it must start with `-` **and** have at least one
character after it. The second half is what keeps a bare `-` out of this
function, and that matters — bash treats `export -` not as an option but as a
name, printing `bash: export: '-': not a valid identifier` with status **1**.
Because `invalid_option` returns 0 for it, `-` falls through to
`env_key_is_valid`, which rejects it (`-` is neither alphabetic nor `_`), and the
caller produces exactly that message and status. Getting this branch wrong is the
difference between returning 1 and returning 2.

```c
opt[0] = '-';
opt[1] = arg[1];
opt[2] = '\0';
```
A three-byte stack buffer holding just the first offending flag letter. Only
`arg[1]` is copied because that is what bash reports: `export -xyz` prints
`export: -x: invalid option`, naming the first character `getopt` choked on, not
the whole cluster. The buffer is automatic storage, so there is nothing to free
and no allocation that could fail — which is the reason it is a `char[3]` rather
than an `ft_strjoin`.

```c
shell_error("export", opt, "invalid option");
ft_putendl_fd("export: usage: export [-fn] [name[=value] ...] or export -p",
    STDERR_FILENO);
return (1);
```
Two lines on stderr, matching bash byte for byte — the usage string is copied
verbatim from bash 5.2.21, including the `[-fn]` and the `or export -p` tail.
Note the usage line does **not** go through `shell_error`, so it has no
`minishell: ` prefix; bash's second line likewise has no `bash: ` prefix.

The return value is a boolean "this was a bad option", not a status; the caller
translates it into `EXIT_MISUSE`.

Only `argv[1]` ever reaches this function — see the caller — so a `-` word that
appears after a real operand is not tested here at all.

A real divergence to own: because any leading `-`-prefixed argument lands here,
the options bash actually supports (`-p`, `-n`, `-f`) are rejected too.
`export -p` in bash prints the same listing as bare `export`; here it prints
"invalid option" and returns 2. Supporting `-p` is not in the mandatory subject.

### `int builtin_export(char **argv, t_shell *sh)`

```c
if (!argv[1])
    return (export_list(sh->env));
```
Bare `export` prints the sorted listing. This is checked before any option
scanning, so it is the only path that produces output on stdout. `export_list`
returns 0 normally and 1 only if its temporary array allocation failed.

```c
if (invalid_option(argv[1]))
    return (EXIT_MISUSE);
```
The option scan runs **once, on the first argument only, before the loop**. That
placement is the whole of bash's option model: `internal_getopt` stops at the
first word that is not an option, so from that point on every remaining word is
an operand and a leading `-` on it means nothing special. Testing `argv[1]` alone
reproduces that exactly, and it is the reason all three of these agree with bash
5.2.21:

- `export -x A=1` — the flag comes first, so it is a flag: `invalid option`,
  status 2, and `A` is not set.
- `export A=1 -x` — `A=1` ends option parsing, so `-x` is an operand and fails
  the identifier test: `export: '-x': not a valid identifier`, status 1, and `A`
  **is** set.
- `export A=1` — no option scan fires at all.

An earlier version of this file called `invalid_option(argv[i])` inside the loop,
which rejected a `-` word wherever it appeared and gave status 2 for the middle
case. That was the one place `export` disagreed with bash on argument order.

The early return is the asymmetry to be able to justify: an unknown option is a
usage error, so bash abandons the builtin entirely rather than doing half the
work. `EXIT_MISUSE` is 2 in `includes/minishell.h`, and 2 is bash's status for
builtin usage errors. Because the check precedes the loop, nothing has been
assigned when it fires — `export -x A=1` really does leave `A` untouched. Nothing
has been allocated locally either, so there is nothing to free on this path.

The `argv[1]` dereference is safe without a guard because the bare-`export` case
returned two lines above.

```c
i = 1;
status = EXIT_OK;
while (argv[i])
{
```
`status` is a running accumulator, deliberately not an early return. It starts at
0 and is only ever raised to 1; it is never lowered, so one bad name among ten
good ones still yields 1 while the nine good ones are applied. This is the "keeps
processing the remaining arguments" behaviour of bash: `export 1A=2 B=3` prints
one error, sets `B` to 3, and exits 1 — verified directly.

The loop starts at index 1, not 2: `argv[1]` was only *inspected* by
`invalid_option`, never consumed, so when it is not an option it still has to be
processed as a name like any other.

```c
    if (!env_key_is_valid(argv[i]))
    {
        shell_error("export", argv[i], "not a valid identifier");
        status = 1;
    }
```
`env_key_is_valid` (in `path-b/env/env_export.c`) validates only the key portion:
the first character must be a letter or `_`, the rest letters, digits or `_`,
stopping at the first `=` — and it returns early with success when it sees `+=`,
which is how the append form is admitted. So `1A=2`, `+A=1` and `A-B=1` are all
rejected here, exactly as bash rejects them.

The whole argument, not just the key part, is put in the message, because that is
what bash quotes back: `export: '1A=2': not a valid identifier`. (Bash wraps the
name in backquote-quote; `shell_error` does not, which is a cosmetic difference.)

Note this branch does **not** `continue` past the increment — it simply skips the
`else`, so the loop advances normally.

```c
    else if (env_set_from_string(&sh->env, argv[i]) < 0)
        status = 1;
    i++;
}
return (status);
```
`env_set_from_string` does the real work: it splits at the first `=`, calls
`env_append` when the character before the `=` is `+` (so `A+=B` concatenates
onto whatever `A` held, or onto `""` if it held nothing), and calls `env_set`
otherwise. With no `=` at all it calls `env_set(env, arg, NULL)`, creating the
"declared but not exported" node — that is where `export X` gets its `NULL`
value from, and it is why `X` then shows in `export` output but not in `env` and
not in a child's environment.

A negative return means `malloc` failed somewhere inside; it is folded into
status 1 rather than aborting, so the remaining arguments are still attempted.
Ownership is clean: `env_set` and `env_append` copy what they store, and
`env_append`'s intermediate `ft_strjoin` result is freed inside `env_append`
itself, so `builtin_export` allocates nothing and frees nothing.

One subtle detail belonging to `env_append` but visible from here:
`env_append` writes a `'\0'` over the `+` in the key buffer. That buffer is the
`ft_substr` copy made by `env_set_from_string`, not `argv[i]`, so the command
line is not mutated and `free_cmds` is unaffected.

The final `return (status)` gives 0 if every argument was applied, 1 if at least
one name was invalid or an allocation failed. Status 2 can only come from the
option path.

## Things to be ready to explain

- **Why does an invalid option return 2 but an invalid identifier return 1?**
  Bash separates usage errors from data errors. An unrecognised option means the
  command was invoked wrongly, so the builtin does nothing and returns 2; a bad
  variable name is just one rejected operand, so bash reports it, keeps going
  with the rest, and returns 1 at the end. `export 1A=2 B=3` really does set `B`
  in bash — checked against 5.2.21.
- **Why is a bare `-` not treated as an option?** `invalid_option` requires a
  character after the `-`. Bash agrees: `export -` gives
  `'-': not a valid identifier` and status 1, not "invalid option" and 2.
- **What does `export X` alone actually store, and why doesn't a child see it?**
  `env_set_from_string` finds no `=` and calls `env_set(..., NULL)`, producing a
  node with `value == NULL`. `env_to_envp` and `builtin_env` both skip
  `value == NULL` nodes, so the variable is listed by `export` but never reaches
  `execve`'s environment. That is bash's "declared but not exported" state.
- **How does `export A+=B` work?** `env_key_is_valid` returns success as soon as
  it sees `+=`, then `env_set_from_string` notices `eq[-1] == '+'` and calls
  `env_append`, which reads the old value (or `""`), joins the new text onto it
  and stores the result. `A=x; export A+=B` leaves `A=xB`.
- **Is any state left half-modified when `export` fails?** Yes, and on purpose.
  Arguments are applied left to right, so anything before the failure is already
  in the environment. There is no rollback, and bash does not roll back either.
  No memory is leaked, because `builtin_export` owns no allocation: everything
  is copied into, and owned by, the `t_env` list.
- **Where does `export -x` behave differently from bash?** Only in which options
  exist. `export -x` matches bash exactly (message, usage line, status 2), and so
  does `export A=1 -x`, which gives `'-x': not a valid identifier` and status 1
  because the scan stops at the first operand just as bash's does. What still
  differs is that `export -p`, `-n` and `-f` are rejected here, where bash
  honours them; none is in the mandatory subject.
