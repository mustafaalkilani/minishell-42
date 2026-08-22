# builtin_echo.c

Implements `echo` with the single option the subject requires, `-n`. The only
non-obvious part is deciding what counts as the `-n` flag: bash accepts a run of
`n`s (`-nnn`) and any number of consecutive flag arguments (`-n -n -n`), but
treats anything else starting with `-` as an ordinary word to print. Everything
is written straight to `STDOUT_FILENO`; redirection has already been applied by
the caller, so this file never opens or closes a descriptor.

## Walkthrough

### `static int is_n_flag(const char *arg)`

```c
if (arg[0] != '-' || arg[1] != 'n')
    return (0);
```
Two rejections in one test. The first excludes any word that is not an option at
all. The second excludes the bare string `"-"`, because `arg[1]` is then `'\0'`,
which is not `'n'` — and bash indeed prints a lone `-` as a normal argument.
Reading `arg[1]` after checking `arg[0] != '-'` is safe: `arg[0]` was not the
terminator, so index 1 is still inside the allocation (worst case it is the NUL).

```c
i = 1;
while (arg[i] == 'n')
    i++;
return (arg[i] == '\0');
```
Skip the whole run of `n`s and demand that the string ended exactly there. This
is what makes `-nnn` a flag and `-na` a plain argument. The rule matters because
bash's `echo` deliberately does *not* have a general option parser: the moment a
word is not entirely `-` followed by `n`s, option processing stops and that word
plus everything after it is printed verbatim. So `echo -na hi` prints `-na hi`
with a trailing newline, and `echo -nnn hi` prints `hi` with no newline.

Starting the loop at `i = 1` rather than `i = 2` is harmless — position 1 is
already known to be `'n'`, so the loop simply re-tests it once.

### `int builtin_echo(char **argv)`

```c
i = 1;
newline = 1;
```
Index 1 skips `argv[0]`, which is the literal word `echo`. `newline` is the
default-on trailing newline; the flag only ever turns it off, never back on.

```c
while (argv[i] && is_n_flag(argv[i]))
{
    newline = 0;
    i++;
}
```
Consumes a *leading run* of flag words. The `&&` short-circuit stops at the
first non-flag word, which is precisely bash's "option processing ends at the
first non-option" rule. This is why `echo hi -n` prints `hi -n` followed by a
newline: the `-n` is no longer leading, so it is data, not an option.

Setting `newline = 0` repeatedly is idempotent — `echo -n -n -n hi` behaves like
`echo -n hi`, matching bash.

Note the edge case `echo -n` with nothing after it: the loop consumes the flag,
the print loop below runs zero times, `newline` is 0, so absolutely nothing is
written and the exit status is still 0. Bash agrees.

```c
while (argv[i])
{
    ft_putstr_fd(argv[i], STDOUT_FILENO);
    if (argv[i + 1])
        ft_putchar_fd(' ', STDOUT_FILENO);
    i++;
}
```
Arguments are joined with a single space, and the lookahead `argv[i + 1]` is
what prevents a trailing space after the last one. Reading `argv[i + 1]` is safe
because `argv` is NULL-terminated by the parser: when `argv[i]` is the last real
argument, `argv[i + 1]` is the terminating `NULL`, which is a valid read.

The separator is always exactly one space regardless of what the user typed.
`echo a     b` prints `a b`, because the whitespace was consumed by the lexer
when it split words; by the time echo sees them they are two separate `argv`
entries. Quoting changes this — `echo "a     b"` is one argument and prints its
internal spaces intact — and that is handled entirely in path-a, not here.

Nothing is freed in this loop: the strings belong to `cmd->argv`, owned by the
`t_cmd` list, and are released by `free_cmds` after the line finishes. `echo`
allocates nothing at all, so it has no error path and no cleanup obligations.

```c
if (newline)
    ft_putchar_fd('\n', STDOUT_FILENO);
return (EXIT_OK);
```
`EXIT_OK` is 0 from `includes/minishell.h`. `echo` returns 0 unconditionally in
this implementation — there is no path that returns anything else. Real bash can
return 1 if the write fails (for example `echo hi > /dev/full`), which is not
detected here because `ft_putstr_fd` discards the `write` return value. That is a
known, deliberate simplification, not something the subject tests.

## Things to be ready to explain

- **Why does `echo -na hi` print `-na hi` but `echo -nnn hi` print `hi`?**
  `is_n_flag` requires the argument to be `-` followed by *only* `n`s up to the
  terminator. `-nnn` qualifies, `-na` does not, so `-na` is treated as data.
- **Why does `echo hi -n` still print a newline?** Option scanning stops at the
  first argument that is not a flag. Once `hi` has been seen, `-n` is an ordinary
  word and is printed literally. Bash behaves the same way.
- **Why is `argv[i + 1]` safe to dereference in the print loop?** `argv` is
  NULL-terminated for `execve`, so the element after the last argument always
  exists and is `NULL`. The test is what suppresses the trailing space.
- **Can `echo` ever fail?** Not in this implementation: it always returns
  `EXIT_OK` (0). Bash returns 1 on a write error such as a full disk; that case
  is not detected because the `write` return value is not checked.
- **Where did the extra spaces in `echo a     b` go?** They were consumed by the
  lexer during word splitting. `echo` only ever re-joins its arguments with one
  space each; it never sees the original spacing.
- **Does `echo` need `t_shell`?** No. It performs no expansion (that already
  happened in path-a) and touches no shell state, so `run_builtin` passes it only
  `argv`.
