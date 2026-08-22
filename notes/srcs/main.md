# main.c

The entry point and the read-eval-print loop. It owns the `t_shell` state
struct on `main`'s stack, drives one full pass of lex -> expand -> parse ->
execute per input line, and decides the process exit status. Everything else
in the project is reached from here, so this file is the map of the whole
program.

## Walkthrough

### `static void shell_init(t_shell *sh, char **envp)`

```c
sh->env = env_init(envp);
```
Turns the `char **envp` that the kernel handed to `main` into the linked list
of `t_env` nodes that the rest of the shell uses. We convert once at startup
rather than parsing `envp` on every lookup, because `export` and `unset` have
to mutate the environment and a flat `char **` is painful to insert into or
delete from. `env_init` also bumps `SHLVL`, which is why it needs the original
array rather than an empty list.

```c
sh->last_status = 0;
```
`$?` starts at 0. Bash does the same: a freshly opened shell that has run
nothing reports success.

```c
sh->stdin_backup = -1;
sh->stdout_backup = -1;
```
Sentinel values meaning "no fd saved". `-1` is used rather than `0` because
`0` is a perfectly valid fd (stdin), so zero-initialising would look like a
saved descriptor and a later `close()` would shut stdin.

```c
sh->current_cmds = NULL;
sh->current_line = NULL;
```
The two handles that `shell_cleanup` in `builtin_exit.c` frees on its way out.
Both were previously left as whatever the stack happened to hold. In practice
nothing read them before they were assigned — `exec_line` sets `current_cmds`
before any command can run, and `shell_loop` sets `current_line` before any
command can be parsed — but `shell_cleanup` and `close_other_heredocs` both
dereference `current_cmds`, and depending on call ordering to keep an
uninitialised pointer from being read is exactly the kind of invariant that
breaks the first time someone adds a code path. Two assignments make it a
non-question.

Note what this function deliberately does *not* do: there is no allocation of
a heap `t_shell`. The struct lives on `main`'s stack, so it cannot leak and
it disappears automatically when the process ends.

### `static t_cmd *build_cmds(char *line, t_shell *sh)`

This is the whole of path-a in five statements, and the ordering is the part
worth defending.

```c
tokens = lex(line);
if (!tokens)
    return (NULL);
```
The lexer cuts the raw line into `t_token` nodes, each word carrying its
parallel `quotes[]` array. A `NULL` return means either an empty line (already
filtered by the caller) or a lexing failure such as an unclosed quote, and the
lexer has already printed its own message. We only propagate the failure; we
never print a second one, because bash prints exactly one diagnostic per bad
line.

```c
if (expand(tokens, sh) != 0)
{
    free_tokens(tokens);
    return (NULL);
}
```
Expansion happens **on the token list, before parsing**. This is the single
most important design decision in the front end and evaluators ask about it.
The reason is the `quotes[]` metadata: it is per-character and it only exists
while the token still exists. `$USER` inside double quotes must not word-split
and `$USER` unquoted must, and the only way to know which is to look at the
quote byte sitting under the `$`. Once the parser has flattened tokens into
`cmd->argv` strings, that information is gone. Expanding first also means
`split_token` can turn one token into several tokens in place, which is far
easier than trying to insert extra `argv` entries later.

On failure we free the tokens ourselves. `expand` does not free its input on
error, so ownership stays here — the function that allocated the list is the
function that releases it.

```c
cmds = parse(tokens);
free_tokens(tokens);
return (cmds);
```
The parser reads the already-expanded token list and builds the `t_cmd`
pipeline. It copies every string it needs (`cmd_add_arg` duplicates), so the
token list is dead the moment `parse` returns and we free it unconditionally —
on the success path *and* the syntax-error path, since `free_tokens(NULL)` is
not a concern here (`tokens` is non-NULL by this point). Freeing in one place
instead of inside both branches keeps the leak surface to a single line.

### `static void process_line(char *line, t_shell *sh)`

```c
if (line_is_blank(line))
    return ;
```
A line of only spaces and tabs is not an error and must not touch `$?`. Bash
leaves the previous status alone for a blank line, so we return before any
status assignment. Filtering here rather than inside `lex` has a second
benefit: it makes the `NULL` from `build_cmds` unambiguous.

```c
cmds = build_cmds(line, sh);
if (!cmds)
{
    sh->last_status = EXIT_MISUSE;
    return ;
}
```
Because blanks were already handled, a `NULL` can only be a syntax error, and
bash reports syntax errors as status 2 (`EXIT_MISUSE`). The message itself was
printed downstream by `syntax_error`, so all we do here is record the code.

```c
sh->last_status = exec_line(cmds, sh);
free_cmds(cmds);
```
Execution returns the status of the last pipeline stage, which becomes `$?`
for the next line. The command list is freed immediately after; nothing in
`t_shell` keeps a live pointer past this point except `current_cmds`, which is
only meaningful during `exec_line` itself.

### `static int shell_loop(t_shell *sh)`

```c
signals_setup_interactive();
```
Re-installed on **every** iteration, not once before the loop. That looks
redundant until you remember that `run_pipeline` calls `signals_ignore()` and
`child_exec` calls `signals_setup_child()`. After a foreground command the
parent's handlers are still set to `SIG_IGN`, so ctrl-C at the next prompt
would do nothing. Reinstalling at the top of each iteration guarantees the
prompt is always in the interactive signal state regardless of what the
previous command did.

```c
line = read_input_line(PROMPT);
if (!line)
{
    if (isatty(STDIN_FILENO))
        ft_putendl_fd("exit", STDERR_FILENO);
    break ;
}
```
`NULL` means end of input: ctrl-D on a terminal, or the end of a pipe when
we are being scripted. Bash prints `exit` on ctrl-D, but only in interactive
mode — that is why the `isatty` guard is there. Without it, every piped test
run would emit a spurious `exit` line that the differential tester would flag
against bash. It goes to **stderr**, again matching bash, so it never pollutes
the stdout that gets diffed.

```c
sh->current_line = line;
```
Published into the struct immediately after the NULL check, purely so that the
`exit` builtin can free it. `builtin_exit` calls `exit()` from inside
`process_line`, which means the `free(line)` at the bottom of this loop never
executes on that path and the readline buffer holding the very line that said
`exit` was still allocated when the process died. Valgrind classified it as
"still reachable" rather than "definitely lost", because this stack variable was
still pointing at it — but `shell_cleanup`'s stated job is to free everything the
shell owns, so it was a real omission. The struct field is the cheapest way to
hand the pointer down without threading a parameter through `process_line`,
`build_cmds` and `run_builtin`.

The ownership rule does not change: `shell_loop` still owns the line, and
`current_line` is a borrowed alias that only the never-returning path consumes.

```c
if (g_signal == SIGINT)
{
    sh->last_status = EXIT_SIG_BASE + SIGINT;
    g_signal = 0;
}
```
The SIGINT handler cannot set `last_status` itself — it is only allowed to
touch the one `volatile sig_atomic_t` global. So the handler records the
signal number and the loop translates it here, after `readline` has returned.
128 + 2 = 130 is bash's convention for "terminated by SIGINT". The flag is
cleared straight away so a single ctrl-C cannot affect two prompts.

```c
if (*line)
    add_history(line);
```
Empty lines are not added to history, matching readline's default behaviour
and bash's. Note this runs even when stdin is a pipe; `add_history` on a
non-tty is harmless and `rl_clear_history` in `main` frees it either way.

```c
process_line(line, sh);
free(line);
sh->current_line = NULL;
```
`readline` returns a malloc'd buffer that the caller owns, and so does our
`read_plain_line` fallback. Freeing it here — in the same function that
received it — is what keeps the loop leak-free across millions of iterations.

Clearing `current_line` back to NULL immediately after the free is what keeps the
alias honest: outside these three lines the field is always NULL, so if
`shell_cleanup` ever runs between two prompts it frees nothing rather than a
dangling pointer. It is the same discipline as the `-1` fd sentinels above.

```c
return (sh->last_status);
```
The loop's value is the status of the last command run, which becomes the
process exit code. That is what makes `false` then ctrl-D exit with 1, one of
the cases in `tests/signals.py`.

### `int main(int argc, char **argv, char **envp)`

```c
(void)argv;
if (argc != 1)
{
    ft_putendl_fd("minishell: takes no arguments", STDERR_FILENO);
    return (EXIT_MISUSE);
}
```
The subject describes an interactive shell only; there is no `-c` mode and no
script argument, so anything on the command line is a usage error. `(void)argv`
silences the unused-parameter warning while keeping the standard three-argument
`main` signature, which we need because `envp` is the third parameter.

```c
shell_init(&sh, envp);
status = shell_loop(&sh);
```
`&sh` is a stack address passed down through every layer. This is how the
project gets away with exactly one global: all mutable state lives in this
struct and is threaded explicitly through parameters.

```c
env_free(sh.env);
rl_clear_history();
return (status);
```
Both cleanups run on the normal exit path. `rl_clear_history` is what stops
valgrind reporting the history list as a definite leak — readline's other
internal buffers are handled by `tests/readline.supp` because we cannot free
them. Note that `builtin_exit` takes a different route out of the program
(it calls `exit()` directly), so it has to do its own cleanup; that is a
known and intentional asymmetry, and `sh->current_line` exists so that its
`shell_cleanup` can cover the one allocation this path would otherwise reach
first.

## Things to be ready to explain

- **Why expand before parse instead of after?** Because expansion needs the
  per-character `quotes[]` array to decide whether a `$` is live, and whether
  its result may word-split. The parser destroys tokens into flat `argv`
  strings, so that metadata would already be gone.
- **Why call `signals_setup_interactive()` inside the loop?** Because
  `run_pipeline` sets the parent's handlers to `SIG_IGN` while a child runs.
  Reinstalling every iteration guarantees the prompt is always interactive
  again, whatever the previous command did to the handlers.
- **Why is `exit` printed only when `isatty`?** Bash prints it only in
  interactive mode. Printing it when stdin is a pipe would add a line that
  the differential tester in `tests/edges.sh` would see as a mismatch. It
  goes to stderr for the same reason.
- **Where does 130 come from?** `EXIT_SIG_BASE + SIGINT`, i.e. 128 + 2. The
  signal handler may only write `g_signal`; the loop converts it into a
  status afterwards, because a handler must not touch the shell struct.
- **Who frees what?** `main` owns `sh.env`; `shell_loop` owns each `line`;
  `build_cmds` owns the token list and frees it before returning; `process_line`
  owns the `t_cmd` list. Every allocation is released by the function one
  level above the one that produced it.
- **Why does `shell_loop` copy `line` into `sh->current_line`?** Because `exit`
  never comes back. It calls `exit()` from inside `process_line`, so the
  `free(line)` at the bottom of this loop is skipped and the input buffer for
  that line would stay allocated until the process died — valgrind's "still
  reachable", not a lost block, but still something the shell owns and claims to
  free. `shell_cleanup` frees the field first; the loop clears it to NULL after
  its own free so the alias is never stale.
- **Why is `t_shell` on the stack?** So it cannot leak and needs no teardown.
  It is passed by pointer everywhere, which is how the project satisfies the
  "one global variable" rule.
