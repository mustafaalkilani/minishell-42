# builtin_cd.c

Changes the shell's working directory and keeps `PWD` and `OLDPWD` in step with
it. It resolves the four argument shapes bash supports for the mandatory part —
no argument means `$HOME`, `-` means `$OLDPWD` (and echoes where it went), `--`
is the POSIX end-of-options marker that also means `$HOME`, and an empty-string
operand (`cd ""`) is a silent no-op — rejects anything else that looks like a
flag, then calls `chdir` and updates the environment only if that succeeded.
Because a lone `cd` runs in the parent process (see `exec_line`), the directory
change and the two variable updates persist to the next prompt.

## Walkthrough

### `static int invalid_option(const char *arg)`

The same three-line shape as the helpers in `builtin_export.c` and
`builtin_unset.c`, with `cd`'s own message and usage line. It exists because
without it `cd -x` fell through to `chdir("-x")` and reported
`No such file or directory` with status 1, while bash reports
`cd: -x: invalid option` and returns 2.

```c
if (arg[0] != '-' || !arg[1])
    return (0);
```
The `!arg[1]` half is what keeps a bare `-` out of this function, and for `cd`
that half is not a nicety — `-` is `cd`'s "go to `$OLDPWD`" operand. If this
returned 1 for `-`, the single most common interactive `cd` form would break.

```c
opt[0] = '-';
opt[1] = arg[1];
opt[2] = '\0';
shell_error("cd", opt, "invalid option");
ft_putendl_fd("cd: usage: cd [-L|[-P [-e]] [-@]] [dir]", STDERR_FILENO);
return (1);
```
A three-byte stack buffer holding only the first offending flag letter, matching
`getopt`, which names the first character it did not recognise: `cd -xyz` says
`cd: -x: invalid option`. The usage string is bash 5.2.21's verbatim, including
the options `cd` actually supports there and this shell does not. It bypasses
`shell_error`, so it carries no `minishell: ` prefix — bash's second line
likewise has no `bash: ` prefix.

The return value is a boolean "this was a bad option", not a status; the caller
turns it into `EXIT_MISUSE`.

The honest divergence is the same as in the other two builtins: because we
support no options at all, the flags bash really does accept (`-L`, `-P`, `-e`,
`-@`) are rejected here rather than honoured. None of them is in the mandatory
subject.

### `static void update_pwd_vars(t_shell *sh, char *old)`

Called **only after a successful `chdir`**. That ordering is the whole point: if
`chdir` failed, `PWD` and `OLDPWD` must not move, because the shell is still
where it was.

```c
if (old)
    env_set(&sh->env, "OLDPWD", old);
```
`old` is the `getcwd` result captured by the caller *before* the `chdir`. The
NULL guard covers the case where `getcwd` failed — which really happens when the
current directory was deleted or made unreadable underneath the shell. In that
situation `OLDPWD` is simply left at its previous value. Bash instead falls back
to `$PWD` there, so this is a small, deliberate-looking divergence in an already
degenerate state.

```c
cwd = getcwd(NULL, 0);
if (cwd)
{
    env_set(&sh->env, "PWD", cwd);
    free(cwd);
}
```
`getcwd(NULL, 0)` is the GNU extension that mallocs a buffer of the right size,
so there is no `PATH_MAX` truncation. The buffer is **owned by us** and is freed
immediately after `env_set`, which takes its own `ft_strdup` copy — that is the
ownership contract of `env_set` and the reason this is not a double-free or a
leak. If `getcwd` fails, `PWD` is left stale rather than being set to garbage.

`env_set`'s return value is ignored on both calls. It returns -1 on allocation
failure, so under memory exhaustion `cd` would move the shell but silently fail
to record `PWD`. Not checked; worth admitting rather than defending.

Both directories come from `getcwd`, never from `$PWD`, which is what the
comment above the function in the source now says as well — it used to claim
`PWD` was read from the environment, which was never true of the code.

### `static char *resolve_target(char **argv, t_shell *sh)`

Returns a **borrowed** pointer: either an env node's `value` field or a pointer
straight into `argv`. Nothing here is allocated, so the caller must not free the
result. That is the single most important fact about this function.

```c
if (!argv[1])
{
    var = env_get(sh->env, "HOME");
    if (!var)
        shell_error("cd", NULL, "HOME not set");
    return (var);
}
```
Bare `cd` goes home. `env_get` returns `NULL` both when `HOME` is absent and when
it was declared without a value (`export HOME` with no `=`), and both are treated
as "not set" — which matches bash, where `unset HOME; cd` prints
`bash: cd: HOME not set` and returns 1. Returning `NULL` rather than calling
`chdir(NULL)` is what stops the crash; the caller turns it into status 1.

```c
if (!ft_strncmp(argv[1], "-", 2))
{
    var = env_get(sh->env, "OLDPWD");
    if (!var)
        shell_error("cd", NULL, "OLDPWD not set");
    else
        ft_putendl_fd(var, STDOUT_FILENO);
    return (var);
}
```
The `2` is again `strlen("-") + 1`, so this matches the argument `-` exactly and
not `-P` or `-abc`. `cd -` is the "jump back" form, and bash prints the directory
it landed in so you can see where you went — that is the `ft_putendl_fd`. The
print goes to `STDOUT_FILENO`, not stderr, because it is data: `cd - > log`
captures it in bash too.

One honest divergence: this prints the `OLDPWD` value *before* attempting the
`chdir`. Bash prints only after a successful change. So if `OLDPWD` points at a
directory that has since been removed, this implementation prints the path and
*then* prints the `chdir` error, whereas bash prints only the error.

```c
return (argv[1]);
```
Any other argument is used literally as the path. Anything that could have been
read as a flag was already dealt with by `invalid_option` in the caller, so what
arrives here is either an operand or something that survived a `--` shift.
`CDPATH` is not implemented; it is not in the mandatory part.

### `static int cd_perform(char *target, t_shell *sh)`

The `chdir`-and-update half of `cd`, extracted from the main function so both
sides stay under the norm's 25-line-per-function limit. Called only after
`builtin_cd` has resolved the target and ruled out the two no-op cases
(unresolved variable, empty-string operand).

```c
old = getcwd(NULL, 0);
```
Captured *before* the `chdir`, because afterwards the old path is unrecoverable.
This is the only allocation in the function, and every path below frees it
exactly once. `getcwd(NULL, 0)` is the GNU extension that mallocs a buffer of
the right size, so there is no `PATH_MAX` truncation.

```c
if (chdir(target) != 0)
{
    shell_error("cd", target, strerror(errno));
    free(old);
    return (1);
}
```
`chdir` is the single point of truth: no stat-then-chdir race, no manual
permission check. `strerror(errno)` reproduces bash's wording exactly, so a
missing directory prints `minishell: cd: /nope: No such file or directory` and a
file prints `Not a directory`, both with status 1. `free(old)` on this path is
what keeps the failure case leak-free — forgetting it is the classic `cd`
valgrind report, because a shell session usually contains several failed `cd`s.

`target` is *not* freed here, and must not be: it is borrowed from `argv` or from
an env node (see `resolve_target`).

```c
update_pwd_vars(sh, old);
free(old);
return (EXIT_OK);
```
Environment update, then release. There is a subtle lifetime rule hiding here:
when the command was `cd -`, `target` points at the `OLDPWD` node's `value`, and
`update_pwd_vars` calls `env_set(&sh->env, "OLDPWD", old)` which **frees that
very string**. So `target` is dangling from that call onwards. It is never read
again — the last use was the `chdir`/error path above — so the code is correct,
but the ordering is load-bearing and an evaluator poking at it deserves a
straight answer. Moving the `shell_error` after `update_pwd_vars` would
introduce a use-after-free.

### `int builtin_cd(char **argv, t_shell *sh)`

```c
if (argv[1] && !ft_strncmp(argv[1], "--", 3))
    argv++;
else if (argv[1] && invalid_option(argv[1]))
    return (EXIT_MISUSE);
```
The two halves are chained with `else if`, and that is load-bearing rather than
stylistic. Written as two independent statements, the option test would still run
after the `--` shift and would then see the *post-marker* argument, so `cd -- -x`
would be rejected as a bad flag. As an `else if`, a leading `--` consumes the
option-parsing opportunity entirely, and `cd -- -x` goes on to attempt
`chdir("-x")` and fail with `No such file or directory` and status 1 — which is
what bash does, because `--` ends option parsing there too.

Only `argv[1]` is tested, never the later arguments. Bash parses builtin options
with `getopt`, which stops at the first operand, so once a non-option word has
been seen nothing after it can be a flag. That is why `cd` (and `export`, and
`unset`) checks exactly once, before doing any work, instead of inside a loop.

`--` is the POSIX end-of-options marker. Incrementing the local `argv` shifts the
whole array by one so that everything below sees the arguments *after* the
marker: `cd -- /tmp` becomes `cd /tmp`, and `cd --` on its own becomes `cd` with
no argument, i.e. `$HOME`. This mutates only the local copy of the pointer —
`char **` is passed by value — so `cmd->argv` in the caller is untouched and
`free_cmds` still frees the original array from its original base. That would be
a genuine double-free/heap-corruption bug if `argv` were a pointer to the
caller's variable, so it is worth stating explicitly.

Note that after the shift, `cd -- -` still resolves through the `-` branch and
goes to `$OLDPWD`. That looks wrong for a strict end-of-options reading, but it
is what bash does as well: bash's `cd` applies its `-` special case to the
operand *after* option parsing.

```c
if (argv[1] && argv[2])
{
    shell_error("cd", NULL, "too many arguments");
    return (1);
}
```
Bash prints `bash: cd: too many arguments` and returns **1** for `cd a b`. Note
this check runs after the `--` shift, so `cd -- a b` is also rejected, correctly.
Nothing has been allocated at this point, so returning early leaks nothing.

```c
target = resolve_target(argv, sh);
if (!target)
    return (1);
```
`NULL` means `resolve_target` already printed the "HOME not set" or
"OLDPWD not set" diagnostic, so the caller only supplies the status. **1** is
bash's status for every `cd` failure.

```c
if (target[0] == '\0')
    return (EXIT_OK);
```
`cd ""` is a silent no-op. Bash treats an empty operand as "do nothing, return
0" — `chdir("")` would fail with `ENOENT`, so without this line the shell would
print `minishell: cd: : No such file or directory` and return 1, which diverges
loudly from bash and shows up on real evaluators. Guarding it here rather than
inside `cd_perform` keeps `cd_perform` focused on the "we really are moving"
case and short enough to stay under the norm's line limit.

Two subtleties. First, this check runs *after* `resolve_target`, so `cd -` where
`OLDPWD` was `export`ed as `""` is also treated as a no-op — matching bash,
which prints an empty line and returns 0 in the same situation. Second, this
does not touch `OLDPWD`: because `chdir` never ran, there was no move to
record. Bash agrees; a `cd ""` does not leak into the next `cd -`.

```c
return (cd_perform(target, sh));
```
Everything from here on lives in `cd_perform`: the `getcwd` capture, the
`chdir`, and the `PWD` / `OLDPWD` update. `EXIT_OK` is 0. `cd` returns 0 on
success and on the two no-op cases; 1 for too many arguments, for
`HOME`/`OLDPWD` not set, and for any `chdir` failure; 2 only from the option
check at the top.

## Things to be ready to explain

- **Why does `cd` in a pipeline not move the shell?** `exec_line` only runs a
  builtin in the parent when it is alone on the line. Inside a pipeline it is
  reached from `child_exec` after a `fork`, so the `chdir` and the `PWD`/`OLDPWD`
  updates happen in the child and vanish when it exits. Bash does exactly this,
  which is why `cd /tmp | true; pwd` still prints the old directory in both
  shells.
- **What does `cd --` do, and why `argv++`?** `--` is the POSIX end-of-options
  marker, so `cd --` means `cd` with no operand, i.e. `$HOME`, and `cd -- foo`
  means the directory literally named `foo`. `argv++` shifts the local pointer so
  the rest of the function sees the post-marker arguments. It is a local copy, so
  `cmd->argv` and therefore `free_cmds` are unaffected.
- **Why is the option check an `else if` on the `--` branch?** Because `--` ends
  option parsing, so nothing after it may be read as a flag. Chaining the two
  means `cd -- -x` skips `invalid_option` entirely and tries `chdir("-x")`,
  failing with `No such file or directory` and status 1 — bash's behaviour. Two
  separate statements would instead reject `-x` as an option and return 2.
- **Why does the option check only look at `argv[1]`?** Bash parses builtin
  options with `getopt`, which stops at the first operand. A `-` word appearing
  after a real argument is an operand, not a flag. Checking once, before the
  loop, is what makes `cd`, `export` and `unset` agree with bash on argument
  order rather than only when the flag comes first.
- **Why does `cd -` print something?** Bash prints the directory it jumped to so
  the user can see the result of an invisible variable lookup. It goes to stdout
  because it is output, not an error. Caveat: this implementation prints it
  before the `chdir`, so a failed `cd -` prints the path and then the error,
  where bash prints only the error.
- **Who owns the string returned by `resolve_target`?** Nobody new — it is
  borrowed from `argv` or from an `t_env` node's `value`. Freeing it would
  corrupt the environment list or the command list. The only allocation in the
  file is `getcwd(NULL, 0)`, freed on both the success and the `chdir`-failure
  path.
- **What status does `cd` return, and when?** 0 on success; 1 for too many
  arguments, for `HOME`/`OLDPWD` not set, and for any `chdir` failure; 2 for an
  unrecognised option. `cd -x` used to fall through to `chdir("-x")` and report
  `No such file or directory` with status 1; `invalid_option` now catches it and
  returns 2, matching bash.
- **Why capture `old` with `getcwd` before `chdir` instead of reading `$PWD`?**
  Because `$PWD` is user-writable (`export PWD=/lies`) and would then record a
  false `OLDPWD`. `getcwd` asks the kernel. The trade-off is that `getcwd` fails
  if the current directory was deleted, in which case `OLDPWD` is left unchanged
  — bash falls back to `$PWD` there.

- **Why is `cd ""` a no-op instead of an error?** Because that is what bash
  does. `chdir("")` fails with `ENOENT`, so the natural behaviour would be to
  print `cd: : No such file or directory` and return 1 — but bash short-circuits
  this at the shell level and returns 0 silently, so we do too. The
  `target[0] == '\0'` check sits between `resolve_target` and `cd_perform`
  precisely because it is a shell-level rule that should never reach `chdir`.
  Evaluators do test this one, and it costs two lines to get right.

- **Why is `cd_perform` split out at all?** The `--` shift, `invalid_option`
  guard, "too many arguments" check, `resolve_target` call, no-target guard, and
  empty-string guard are already six branches — adding the four-line `getcwd` +
  `chdir` + `update_pwd_vars` block on the end pushed the function past the
  norm's 25-line-per-function limit. Splitting the "we know we're moving" half
  into its own helper keeps both sides short, and reads more like two clean
  paragraphs than one long one.
