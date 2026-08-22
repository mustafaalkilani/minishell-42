# builtin_exit.c

Terminates the shell with a status derived from its argument. It is the only
builtin that normally never returns, and the only one that has to free the whole
shell before leaving, so it owns both the argument parsing and the teardown. Two
pieces deserve real attention: the hand-rolled overflow check in
`parse_exit_code`, which must reject anything outside `long long` *before* the
overflow happens, and the final `((code % 256) + 256) % 256` conversion, which is
why `exit 300` yields 44 and `exit -1` yields 255. All the statuses below were
checked against bash 5.2.21.

The file holds five functions, which is norminette's per-file maximum — worth
knowing before adding anything else to it.

## Walkthrough

### `static const char *skip_blanks(const char *s)`

```c
while (*s == ' ' || (*s >= '\t' && *s <= '\r'))
    s++;
return (s);
```
An `isspace` equivalent written inline, because libft has no `ft_isspace`. The
range `'\t'`..`'\r'` is the contiguous block of tab, newline, vertical tab, form
feed and carriage return (0x09-0x0D), so the two tests together cover exactly the
six characters C calls whitespace. Returning a pointer rather than an index lets
both callers write `s = skip_blanks(s)` and forget the offset.

It exists because bash validates the argument with `strtol` (through
`legal_number`), and `strtol` skips leading blanks by definition; bash then
requires that only blanks follow the digits. Reproducing that is the whole reason
this function was added: `exit " 42 "` exits with 42 in bash, and now here too.

### `static int is_numeric_arg(const char *s)`

```c
s = skip_blanks(s);
i = 0;
if (s[i] == '+' || s[i] == '-')
    i++;
if (!ft_isdigit(s[i]))
    return (0);
```
Leading blanks first, then an optional single sign, then a mandatory digit. The
`ft_isdigit` test does two jobs at once: it rejects `"+"` and `"-"` standing
alone — without it the digit run below would be empty and the function would
cheerfully report success, ending the shell with status 0 instead of an error —
and it rejects an argument that is nothing but blanks, so `exit "  "` and
`exit ""` both give `numeric argument required` and status 2, as in bash.

```c
while (ft_isdigit(s[i]))
    i++;
return (*skip_blanks(s + i) == '\0');
```
The digit run is consumed, and then everything after it must be blanks up to the
terminator. This is the asymmetry that matters: blanks are allowed around the
number but nothing else is, so `exit "42 x"` is still rejected. Hex (`0x10`) is
refused too, since `x` is neither a digit nor a blank.

The worked cases, all matching bash 5.2.21:

- `exit " 42 "` → 42, `exit "<tab>42"` → 42 — surrounding blanks skipped.
- `exit "42 x"` → 2, `exit ""` → 2, `exit "  "` → 2 — trailing junk, or no digits
  at all.
- `exit "+7"` → 7, `exit "-1"` → 255 — the sign is read before the digits.

Quoting is the only way to reach any of these, since the lexer would otherwise
split a word containing a space into two arguments.

The function is a pre-filter only. It says nothing about range — that is
`parse_exit_code`'s job — which is why the two are separate.

### `static int parse_exit_code(const char *s, long long *out)`

Returns 1 and writes through `out` on success, 0 on **either** "not a number" or
"out of range". Collapsing the two is deliberate: bash reports both with the same
message and the same status 2, confirmed with
`exit 9223372036854775808`, which gives `numeric argument required` and `$?` 2.

```c
if (!is_numeric_arg(s))
    return (0);
s = skip_blanks(s);
neg = (s[0] == '-');
i = 0;
if (s[0] == '+' || s[0] == '-')
    i++;
```
Shape check first, then the leading blanks are skipped **again**. The re-skip is
necessary rather than redundant: `is_numeric_arg` took `s` by value and its own
advanced pointer died with it, so without this line `s[0]` here would still be a
space and `neg` would be wrong for `exit " -1"`. `s` is a local `const char *`
parameter, so reassigning it does not disturb the caller's `argv[1]`.

`neg` is 0 or 1 and is about to be used arithmetically, which is the trick in the
next line.

```c
limit = 9223372036854775807ULL + (unsigned long long)neg;
```
The magnitude limit, accumulated in **unsigned** arithmetic. For a positive
number the largest acceptable magnitude is `LLONG_MAX` = 9223372036854775807.
For a negative number it is one larger, 9223372036854775808, because `LLONG_MIN`
is `-(LLONG_MAX + 1)`. Adding `neg` expresses that asymmetry in one expression
instead of an `if`, which keeps the function inside Norminette's variable and
line budget.

The literal is written out rather than using `LLONG_MAX` from `<limits.h>`;
`limits.h` is not among the headers `minishell.h` includes, so the constant is
inlined. The `ULL` suffix is required — without it the literal plus one would
overflow a signed type.

`acc` is `unsigned long long` for the same reason: signed overflow is undefined
behaviour, so the accumulator must be unsigned even though the result is signed.

```c
acc = 0;
while (ft_isdigit(s[i]))
{
    if (acc > (limit - (unsigned long long)(s[i] - '0')) / 10)
        return (0);
    acc = acc * 10 + (unsigned long long)(s[i] - '0');
    i++;
}
```
The loop condition is `ft_isdigit(s[i])`, not `s[i]`. Since blanks are now
tolerated after the number, the string does not necessarily end at the last
digit, and a `while (s[i])` would try to fold `' ' - '0'` into the accumulator.
`is_numeric_arg` has already guaranteed that whatever stops this loop is either
the terminator or blanks, so no validation is repeated here — the condition only
has to stop in the right place.

The body is the line to be able to derive on a whiteboard. The next value would be
`acc * 10 + d`, and it must not exceed `limit`. Rearranging:

```
acc * 10 + d <= limit
acc * 10     <= limit - d
acc          <= (limit - d) / 10        (integer division is exact here,
                                         because acc is an integer)
```
so the *rejection* condition is `acc > (limit - d) / 10`. The point of writing it
this way is that every quantity in the test is computed **before** the
multiplication, so the overflow is predicted rather than detected after the fact.
Checking afterwards — `if (acc * 10 + d < acc)` — would already have relied on
wraparound, which is only defined because `acc` is unsigned and is in any case
harder to justify.

`limit - d` cannot underflow: `limit` is about 9.2e18 and `d` is at most 9.

Leading zeros are harmless — `exit 0000000000000000000000005` still parses as 5,
because `acc` stays 0 through the zeros and never trips the test.

```c
*out = (long long)acc;
if (neg)
    *out = -*out;
return (1);
```
The conversion back to signed. For every input except one this is an ordinary
in-range conversion. The exception is `exit -9223372036854775808`: `acc` then
holds 9223372036854775808, which is `LLONG_MAX + 1`, so `(long long)acc` is an
**implementation-defined** conversion, and negating the resulting `LLONG_MIN` is
technically undefined. On gcc/x86-64 both steps are two's-complement wraparound
and the pair cancels out to exactly `LLONG_MIN`, which is the value we wanted;
the final modulo then gives 0, and bash also gives 0 for that input. So the
observable behaviour is right, but be honest that this single input relies on
implementation behaviour rather than on the standard. A strictly conforming
version would keep the magnitude unsigned all the way into the modulo step.

`*out` is only written on the success path, so a caller that ignores the return
value would read an uninitialised `code` — the caller does check.

### `static void shell_cleanup(t_shell *sh)`

```c
free(sh->current_line);
sh->current_line = NULL;
free_cmds(sh->current_cmds);
sh->current_cmds = NULL;
env_free(sh->env);
sh->env = NULL;
rl_clear_history();
```
Called immediately before every `exit()` in this file, because `exit()` never
returns to `process_line` and `main`, so their own `free`, `free_cmds`,
`env_free` and `rl_clear_history` never run. Without this, every `exit` would
show the input line, the whole command list, the whole environment and the
readline history as leaks.

`sh->current_line` is the readline buffer for the line being executed. It is the
one `shell_loop` is holding in its own local `line` variable and would have freed
after `process_line` returned — except that on this path `process_line` never
returns. `shell_loop` publishes the pointer into the struct immediately after the
NULL check and clears it back to NULL right after its own `free(line)`, so the
field is non-NULL exactly while a line is live and `free(NULL)` covers every
other moment. Before this field existed, `exit 3` left the seven bytes of
`"exit 3"` allocated at process death; valgrind called it **still reachable**
rather than definitely lost, because `shell_loop`'s stack variable still pointed
at it, but the comment above this function claims it frees everything the shell
owns, so it was a genuine miss. `printf 'exit 3\n' | valgrind
--show-leak-kinds=all ./minishell` now reports `still reachable: 0 bytes in 0
blocks`.

`sh->current_cmds` is set by `exec_line` at the very top of the line's execution,
precisely so that this function can reach it. `shell_init` also initialises both
`current_cmds` and `current_line` to NULL, so neither is ever read as stack
garbage — previously the code relied on `exec_line` always assigning
`current_cmds` before any path could reach a free, which was true but an
unguarded invariant, and `close_other_heredocs` dereferences the same field.

Setting the pointers to `NULL` after freeing is defensive rather than necessary —
the process is about to disappear — but it makes the function safe to call twice
and costs nothing.

One thing is still deliberately not freed: `rl_clear_history` handles the history
list, but readline's internal line buffers are the documented exception the
subject allows, and `tests/readline.supp` silences them.

In the pipeline case this runs inside the **forked child**, where it frees the
child's private copies. That is correct and harmless: the parent's list and
environment are untouched in its own address space.

### `int builtin_exit(char **argv, t_shell *sh)`

```c
if (isatty(STDIN_FILENO))
    ft_putendl_fd("exit", STDERR_FILENO);
```
Bash prints `exit` when it leaves an **interactive** shell, and it prints it
*before* parsing the arguments — so interactive `exit 1 2` prints `exit`, then
the error, and then stays alive. That ordering is reproduced here exactly;
verified by piping `exit 1 2` into `bash -i`.

The message goes to stderr so it does not contaminate `./minishell < script`
output, and the same `isatty(STDIN_FILENO)` test appears in `shell_loop` for the
ctrl-D path, keeping the two consistent.

The test is `isatty` rather than a stored "interactive" flag, which introduces a
small divergence: if stdin has been redirected — `exit < file`, or `exit` inside
a pipeline where stdin is a pipe — `isatty` is false and nothing is printed,
whereas bash decides from its own interactive flag.

```c
if (!argv[1])
{
    code = sh->last_status;
    shell_cleanup(sh);
    exit((int)code);
}
```
Bare `exit` inherits `$?`, so `false; exit` leaves with 1. `last_status` is
already in 0..255 (it is either a builtin's small status or `status_to_exit`'s
`128 + signal`), so no modulo is applied here. Cleanup happens before `exit` so
that valgrind sees a clean teardown.

```c
if (!parse_exit_code(argv[1], &code))
{
    shell_error("exit", argv[1], "numeric argument required");
    shell_cleanup(sh);
    exit(EXIT_MISUSE);
}
```
A non-numeric or out-of-range argument **does** terminate the shell, with
`EXIT_MISUSE` = 2. This surprises people who expect an error to keep the shell
alive, but it is bash: `exit abc` prints the message and leaves with 2.

Note this check comes **before** the argument-count check, which is also bash's
order: `exit abc def` reports `abc: numeric argument required` and exits 2; it
does not complain about the extra argument. Reversing the two `if`s would return
1 and keep the shell running, which is wrong.

```c
if (argv[2])
{
    shell_error("exit", NULL, "too many arguments");
    return (1);
}
```
The one path in this file that returns instead of exiting. Bash keeps the shell
alive for `exit 1 2` and sets `$?` to **1** — not 2, even though it is arguably a
usage error. No cleanup is performed here, correctly, because the shell is
continuing and still needs its environment and its command list (which
`process_line` will free in the normal way).

```c
shell_cleanup(sh);
exit((int)(((code % 256) + 256) % 256));
```
The wrap that every evaluator asks about. `exit()` only delivers the low 8 bits
of its argument to the parent through the `wait` status, so a shell must define
what a larger or negative number means; POSIX and bash define it as the value
modulo 256.

Why the double modulo rather than a single `code % 256`? Because C's `%`
truncates toward zero, so for a negative `code` it yields a negative remainder in
the range -255..0. Adding 256 lifts that into 1..511, and the second `% 256`
brings it back into 0..255 without disturbing values that were already positive.
Worked examples:

- `exit 300`: `300 % 256` = 44, `44 + 256` = 300, `300 % 256` = **44**.
- `exit -1`: `-1 % 256` = -1, `-1 + 256` = 255, `255 % 256` = **255**.
- `exit 256`: 0, 256, **0**.
- `exit 255`: 255, 511, **255**.
- `exit -9223372036854775808`: 2^63 is a multiple of 256, so the remainder is 0
  and the result is **0**.

All five agree with bash 5.2.21. Doing this explicitly rather than relying on the
kernel's own truncation matters for the negative cases, where `exit(-1)` would
still land on 255 but the intent would be invisible, and it makes the value
well-defined before the `int` cast.

## Things to be ready to explain

- **Why does `exit 300` return 44?** The wait status only carries 8 bits, so the
  status is taken modulo 256: 300 - 256 = 44. Bash defines it the same way, and
  `exit 256` gives 0, `exit 255` gives 255.
- **Why `((code % 256) + 256) % 256` instead of `code % 256`?** C's `%` truncates
  toward zero, so `-1 % 256` is `-1`, not 255. Adding 256 and taking the modulo
  again normalises negatives into 0..255 while leaving positives unchanged, so
  `exit -1` correctly yields 255.
- **How does the overflow check work, and why check before multiplying?** The
  test is `acc > (limit - d) / 10`, which is `acc * 10 + d <= limit` rearranged
  so that nothing overflows while it is being evaluated. Detecting afterwards
  would require the multiplication to have already wrapped. `acc` is
  `unsigned long long` because signed overflow is undefined behaviour, and
  `limit` is `LLONG_MAX + neg` so that the extra magnitude available to negative
  numbers is accounted for.
- **What does `exit 99999999999999999999` do?** `parse_exit_code` rejects it as
  out of range and returns 0, which the caller treats identically to a
  non-numeric argument: it prints `numeric argument required` and exits with 2 —
  exactly what bash does.
- **Which `exit` does not exit?** Only `exit 1 2`: bash prints
  `too many arguments`, returns 1, and keeps the shell alive. Every other error,
  including `exit abc`, terminates the shell with status 2. Note the ordering —
  `exit abc def` exits with 2, because the numeric check runs first, as in bash.
- **Why is `shell_cleanup` needed at all, and what does it miss?** `exit()` never
  returns, so `process_line`'s `free_cmds` and `main`'s `free`, `env_free` and
  `rl_clear_history` would never run; without the cleanup every `exit` is a large
  leak. The only thing it still misses is readline's internal buffers, which the
  subject explicitly excuses.
- **Why does `t_shell` carry the current input line?** So that this function can
  free it. `exit` calls `exit()` from inside `process_line`, so `shell_loop`'s
  `free(line)` never executes and the readline buffer for the very line
  containing the `exit` command stayed allocated at process death. `shell_loop`
  now publishes the pointer as `sh->current_line` and clears it after freeing,
  and `shell_cleanup` frees it first. Valgrind reported it as "still reachable"
  rather than "definitely lost", because the stack pointer was still live —
  which made it easy to miss but no less ours.
- **Why does `exit " 42 "` work but `exit "42 x"` not?** Because bash validates
  with `strtol`, which skips leading blanks, and then accepts only blanks after
  the digits. `skip_blanks` and the final `*skip_blanks(s + i) == '\0'` in
  `is_numeric_arg` reproduce exactly that rule, so blanks around the number are
  fine and any other trailing character is `numeric argument required` with
  status 2. `exit ""` and `exit "  "` are also 2, since neither contains a digit.
- **What does `exit` inside a pipeline do?** It runs in the forked child (via
  `child_exec`), so it terminates only that child with the computed status; the
  interactive shell survives. `echo hi | exit 5` leaves the shell running, and
  `$?` is 5 because the pipeline's status is that of its last stage.
