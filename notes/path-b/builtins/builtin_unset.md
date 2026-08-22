# builtin_unset.c

Removes variables from the shell's `t_env` list. The behaviour that surprises
people, and that was checked directly against bash 5.2.21, is that `unset`
**silently ignores** anything that could not name a variable and still returns 0:
`unset 1A`, `unset =` and `unset -` all print nothing and give `$?` of 0. The one
thing that is an error is a real option — a `-` flag in *leading* position, which
aborts the builtin before removing anything and returns 2. Because a lone `unset`
runs in the parent process, the removals persist.

## Walkthrough

### `static int invalid_option(const char *arg)`

Structurally identical to the helper in `builtin_export.c`; the two are separate
functions because the message text and the usage line differ, and because they
live in different translation units.

```c
if (arg[0] != '-' || !arg[1])
    return (0);
```
An option is a `-` followed by at least one character. The `!arg[1]` half is
exactly what makes `unset -` **not** an option — and that matters, because bash
prints nothing at all for `unset -` and returns 0. Here the bare `-` falls
through to the identifier test below, fails it, and is skipped silently. Same
observable result.

```c
opt[0] = '-';
opt[1] = arg[1];
opt[2] = '\0';
shell_error("unset", opt, "invalid option");
ft_putendl_fd("unset: usage: unset [-f] [-v] [name ...]", STDERR_FILENO);
return (1);
```
A three-byte stack buffer, so nothing is allocated and there is no failure path.
Only the first flag character is echoed, matching bash's `getopt`, which reports
the first character it did not recognise: `unset -xyz` says `unset: -x: invalid
option`.

One cosmetic divergence: bash 5.2.21 prints
`unset: usage: unset [-f] [-v] [-n] [name ...]` — with `[-n]` — while this string
omits it. The message text and the status are otherwise identical. Also, because
any leading `-`-prefixed argument reaches this function, the options bash really
supports are rejected too: `unset -v A` returns 2 here, where bash unsets `A` and
returns 0. Neither `-v` nor `-f` is in the mandatory subject.

The return value is a boolean "this was an option", not a status; the caller
converts it.

### `int builtin_unset(char **argv, t_shell *sh)`

```c
if (argv[1] && invalid_option(argv[1]))
    return (EXIT_MISUSE);
```
The only early exit and the only non-zero status in the file. `EXIT_MISUSE` is 2
in `includes/minishell.h`, and 2 is bash's status for a builtin usage error.

Note where this test sits: **before the loop**, and on `argv[1]` only. Bash's
`internal_getopt` stops parsing options at the first word that is not one, so a
`-` word appearing after an operand is an operand itself. Testing only the first
argument reproduces that, and it is what makes these two agree with bash 5.2.21:

- `unset -x A` — the flag is leading, so it is a flag: `invalid option`, status 2,
  and `A` is still set, because the check precedes every removal.
- `unset A -x` — `A` ends option parsing, so `-x` is just a name; it fails
  `env_key_is_valid` and is skipped in silence. `A` is removed and the status is
  **0**.

An earlier version called `invalid_option(argv[i])` from inside the loop, which
made the second case remove `A` and then abort with status 2. That was the one
place `unset` disagreed with bash on argument order.

The `argv[1] &&` guard is what allows a bare `unset`, which has no first argument
to inspect. Nothing is allocated in this function, so the early return frees
nothing and leaks nothing.

```c
i = 1;
while (argv[i])
{
```
Starts at 1 to skip the word `unset` itself, and at 1 rather than 2 because
`argv[1]` was only inspected by `invalid_option`, never consumed — when it is not
an option it is a name like any other. There is no "no arguments" special case:
`unset` with nothing after it runs the loop zero times and returns 0, which is
what bash does — a missing name is not an error.

```c
    if (env_key_is_valid(argv[i]) && !ft_strchr(argv[i], '='))
        env_unset(&sh->env, argv[i]);
    i++;
}
```
The heart of the file, and the line to be able to justify twice over.

`env_key_is_valid` is shared with `export`, where it must accept whole
`KEY=VALUE` and `KEY+=VALUE` strings, so on its own it would happily approve
`A=B` as a valid *export* argument. For `unset` that would be wrong: the name of
the variable is `A=B`, which cannot exist. The `!ft_strchr(argv[i], '=')` is the
extra restriction that turns the shared "is this a legal export argument" test
into "is this a legal bare identifier". Without it, `unset A=B` would call
`env_unset` with the key `"A=B"`, which would simply find nothing — harmless in
practice, but the intent is clearer with the guard, and it also blocks the `+=`
form that `env_key_is_valid` deliberately admits.

Everything that fails the test is skipped **without a message**. That is not
laziness: bash really is silent here. `unset 1A`, `unset =`, `unset "A=B"` and
`unset -` each print nothing and return 0 in bash 5.2.21. Compare `export 1A`,
which does print `not a valid identifier` — the two builtins genuinely differ,
and being able to state that difference confidently is the point of this note.

`env_unset` (in `path-b/env/env_ops.c`) unlinks the matching node, frees its
`key`, its `value` and the node itself, and returns 0 whether or not it found
anything. `&sh->env` is passed by address because removing the first node has to
be able to move the head pointer. Since the freeing happens inside `env_unset`,
this builtin owns no memory at any point.

Unsetting a name that does not exist is a no-op and is not an error, which again
matches bash.

One consequence worth knowing: `unset PATH` really does remove `PATH`, after
which `resolve_command` has no directories to search and every external command
becomes "command not found" (127) unless given as a path. That is bash's
behaviour too, and it is a favourite live test.

```c
return (EXIT_OK);
```
0, unconditionally, for every path that did not hit the option check. `unset`
returns only 0 or 2 in this implementation.

## Things to be ready to explain

- **Why is `unset =` silent and why does it return 0?** Because that is what bash
  does — verified against 5.2.21, which prints nothing and gives `$?` of 0 for
  `unset =`, `unset 1A` and `unset -`. Bash's `unset` skips anything that cannot
  name a variable instead of diagnosing it. `export` is the opposite: it reports
  every bad name and returns 1. The two builtins are intentionally asymmetric.
- **Why the extra `!ft_strchr(argv[i], '=')` when `env_key_is_valid` was already
  called?** `env_key_is_valid` is written for `export`, so it accepts a whole
  `KEY=VALUE` or `KEY+=VALUE` argument and only validates the part before the
  `=`. `unset` needs a bare identifier, so the `=` test narrows the shared
  predicate to the stricter meaning.
- **Why does an option return 2 while a bad name returns 0?** A `-x` means the
  command was invoked wrongly, which is a usage error — bash's status for that is
  2, and bash aborts before unsetting anything, so `unset -x A` leaves `A` alone.
  A bad name is just an operand bash chooses to ignore.
- **Does `unset` free anything itself?** No. `env_unset` unlinks the node and
  frees its `key`, `value` and the node in one place, and it receives `&sh->env`
  so it can update the head pointer when the removed node is first. This builtin
  allocates nothing and therefore has no cleanup obligation on any path.
- **Why does `unset` in a pipeline not change the shell?** Same rule as `cd` and
  `export`: `exec_line` only runs a builtin in the parent when it is alone on the
  line. `unset PATH | true` removes `PATH` from the forked child's copy of the
  environment, which is discarded when the child exits — as in bash.
- **Where does this differ from bash?** Two places, both minor: the usage line
  omits bash's `[-n]`, and the genuine options `-v`/`-f` are rejected with 2
  instead of being honoured. Argument order is no longer one of them — the option
  test runs once on `argv[1]` and stops there, so `unset A -x` removes `A` and
  returns 0 exactly as bash does.
