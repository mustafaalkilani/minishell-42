# parser.c

The last stage of path-a. It consumes the already-expanded token list and
produces a linked list of `t_cmd`, one node per pipeline segment, each with a
NULL-terminated `argv` and a list of redirections. It also owns the syntax-error
reporter and the `t_cmd` list destructor. Because it runs **after** the expander,
every word it sees already has its final text — the parser never expands
anything, it only decides what counts as an argument and where a pipeline
segment starts.

## Walkthrough

### `int syntax_error(const char *near)`

```c
ft_putstr_fd("minishell: syntax error near unexpected token `",
    STDERR_FILENO);
ft_putstr_fd((char *)near, STDERR_FILENO);
ft_putendl_fd("'", STDERR_FILENO);
return (-1);
```

Prints bash's exact wording, including the asymmetric backtick-then-apostrophe
quoting that bash uses, and always returns -1.

Returning a value rather than `void` is what lets every call site be written as
`return (syntax_error("|"))`, which keeps the caller a single line and avoids a
separate "print then return" pair everywhere. The `(char *)` cast strips `const`
because libft's `ft_putstr_fd` does not take a `const char *`.

Note it writes to `STDERR_FILENO`, not stdout, so a syntax error inside a
pipeline or with output redirected still reaches the terminal. It does **not**
set the exit status; that happens in `main.c`, which maps a NULL return from
`build_cmds` to `EXIT_MISUSE` (2), matching bash.

This is spelled out longhand instead of going through `shell_error` because the
message shape is different: `shell_error` produces `minishell: ctx: arg: msg`,
which cannot express the backtick quoting.

### `void free_cmds(t_cmd *cmds)`

```c
while (cmds)
{
    next = cmds->next;
    i = 0;
    while (cmds->argv && cmds->argv[i])
    {
        free(cmds->argv[i]);
        i++;
    }
    free(cmds->argv);
    free_redirs(cmds->redirs);
    free(cmds);
    cmds = next;
}
```

Frees the whole command list. Each node owns three things: the strings inside
`argv`, the `argv` array itself, and its redirection list.

The inner loop frees the strings, then `free(cmds->argv)` frees the array of
pointers — doing it in the other order would walk a freed array. The
`cmds->argv &&` guard is defensive: `cmd_new` never returns a node with a NULL
`argv` (it frees the node and returns NULL if that allocation fails), so in
practice `argv` is always at least a one-element array holding the NULL
terminator.

`free_redirs` (in `parser_utils.c`) is delegated to because it does more than
free memory — it also closes any open heredoc file descriptor. That is why
freeing a command list is safe even after path-b has attached heredoc fds to the
redirections.

`next` is saved before `free(cmds)` for the usual reason: reading a field out of
a freed node is a use-after-free.

`free_cmds` is exported in `minishell.h` and is called both by `parse` itself on
the error path and by `main` after `exec_line` returns.

### `static int parse_pipe(t_cmd **cur, t_token **tokens)`

```c
if (!(*cur)->argv[0] && !(*cur)->redirs)
    return (syntax_error("|"));
```

A pipe needs something on its left. `argv[0]` is readable without a bounds check
because `cmd_new` allocates a one-element NULL-terminated array, so `argv[0]` is
NULL for a fresh command. Accepting a command that has redirections but no
arguments is deliberate and matches bash: `> out | cat` is legal.

This single test catches both `| ls` (the head command is still empty) and
`ls | | wc` (the second pipe sees the freshly created, still empty segment).

```c
if (!(*tokens)->next)
    return (syntax_error("newline"));
```

A pipe also needs *something* on its right. When there is nothing at all, bash
interactively opens a continuation prompt; non-interactively and in this shell it
reports the error against the token `newline`, which is exactly the string bash
prints for `ls |` in a script. Note this only checks that a token exists, not
that it is a valid one — `ls | |` passes here and is caught on the next iteration
by the left-hand-side test above.

```c
next = cmd_new();
if (!next)
    return (-1);
(*cur)->next = next;
*cur = next;
*tokens = (*tokens)->next;
return (0);
```

Allocates the next pipeline segment, links it, and moves both cursors forward.
The link is written out here rather than delegated to an append helper because
`*cur` is always the tail of the list, making this O(1) instead of a walk from
the head. The `-1` return on allocation failure is distinguishable from the
syntax errors only by the absence of a printed message, which is a minor
imprecision under OOM.

Both parameters are double pointers because the function has to advance the
caller's cursor variables: `cur` moves onto the new command, `tokens` past the
pipe.

### `static int parse_step(t_cmd **cur, t_token **tokens)`

```c
tok = *tokens;
if (tok->type == T_PIPE)
    return (parse_pipe(cur, tokens));
if (tok->type != T_WORD)
    return (parse_redir_step(*cur, tokens));
```

Three-way dispatch. Anything that is neither a pipe nor a word is one of the four
redirection operators, so `parse_redir_step` gets everything else. Note that
`parse_redir_step` receives `*cur` by value — a redirection never starts a new
command, so the command cursor does not move — but it takes `tokens` by address
because it consumes **two** tokens (the operator and its target).

```c
if (tok->value[0] || tok->had_quotes)
{
    arg = ft_strdup(tok->value);
    if (!arg || cmd_add_arg(*cur, arg) < 0)
        return (-1);
}
*tokens = tok->next;
return (0);
```

This is the empty-word rule, and it is the payoff for all the `had_quotes`
bookkeeping in the lexer. A word whose expanded text is empty is dropped unless
the original text contained a quote character:

- `echo $NOPE` — the expander produced `""`, `had_quotes` is 0, the word
  disappears, and `echo` runs with no arguments and prints a bare newline.
- `echo "$NOPE"` — same empty text, but `had_quotes` is 1, so an empty argument
  is kept and `echo` prints an empty line from that argument.
- `echo ""` — identical mechanism.

`ft_strdup` is mandatory here, not defensive: `main.c`'s `build_cmds` calls
`free_tokens(tokens)` immediately after `parse` returns, so any command holding a
borrowed pointer into the token list would be left dangling. The command list
must own its own strings.

The `ft_strdup` result goes through the local `arg` so it can be tested before
being handed on, and that test is not cosmetic. `cmd_add_arg` stores whatever it
is given at `grown[n]` and returns 0 either way, so a NULL from a failed
`ft_strdup` would be written straight into `argv` as an early NULL terminator:
the argument list would be silently truncated at that point and the parser would
report success. Testing `arg` first turns an out-of-memory condition into a clean
`-1`, which `parse` propagates by freeing the partial list and returning NULL.

Note the short-circuit order in `if (!arg || cmd_add_arg(*cur, arg) < 0)`.
`cmd_add_arg` is only reached when `arg` is non-NULL, so nothing is inserted on
the failure path and there is nothing to clean up — the `-1` return leaves
`argv` exactly as it was. On the other branch `cmd_add_arg` has taken ownership
of `arg` and frees it itself if its own allocation fails, so `arg` is never
leaked and never double-freed.

### `t_cmd *parse(t_token *tokens)`

```c
head = cmd_new();
if (!head)
    return (NULL);
cur = head;
```

A command node is allocated before the loop even starts. That is what makes a
line consisting only of redirections work: `> out` produces one command with an
empty `argv` and one redirection, which is bash behaviour — the file is created
and truncated and nothing is executed. Path-b must therefore tolerate
`argv[0] == NULL`.

```c
while (tokens)
{
    if (parse_step(&cur, &tokens) < 0)
    {
        free_cmds(head);
        return (NULL);
    }
}
return (head);
```

A flat loop with no recursion and no grammar tables — the language is simple
enough that "advance one construct at a time" is a complete parser. Every path
inside `parse_step` either advances `*tokens` or returns negative, so the loop
cannot spin forever.

The failure branch frees the entire partial command list before returning. This
is the early return that also frees; without it every syntax error would leak all
the commands built so far. `head` is freed, not `cur`, because `cur` is an
interior node of the same list.

The NULL return is the only signal to `main`, which turns it into exit status 2.
The message has already been printed by `syntax_error`.

## Things to be ready to explain

- **How do you detect `| ls` and `ls |`?**
  `parse_pipe` checks that the current command already has an argument or a
  redirection (else `syntax error near unexpected token \`|'`) and that a token
  follows the pipe (else the same message with `newline`).

- **Why does `echo "$NOPE"` print a blank line but `echo $NOPE` prints nothing
  extra?**
  Both expand to an empty string, but `had_quotes` is set only for the quoted
  one. `parse_step` keeps an empty word only when `had_quotes` is true, so the
  quoted form contributes an empty argument and the unquoted form contributes
  nothing.

- **Why `ft_strdup(tok->value)` instead of just taking the pointer?**
  `main` frees the whole token list right after `parse` returns. The command list
  has to own its strings or `argv` would point at freed memory during execution.

- **Why is a command node allocated before any token is read?**
  So that a line with only redirections (`> out`) still produces a command. Bash
  creates and truncates the file in that case, and the executor sees an `argv`
  whose first element is NULL.

- **What is the exit status after a syntax error, and where is it set?**
  2. `syntax_error` only prints and returns -1; `parse` returns NULL;
  `process_line` in `main.c` maps that to `EXIT_MISUSE`.

- **`ls | > file` — legal or not?**
  Legal, and it parses here. The left side has an argument, the right side is a
  command with a redirection and no argv, which bash also accepts.

- **Why is the `ft_strdup` result put in a variable and checked, instead of
  passed straight to `cmd_add_arg`?**
  Because `cmd_add_arg` does not test its argument for NULL: it stores it at
  `grown[n]` and returns 0 regardless. Passing a failed `ft_strdup` would write
  a NULL into the middle of `argv`, which acts as an early terminator and
  silently truncates the argument list while reporting success. The explicit
  check makes an out-of-memory failure return -1, which `parse` turns into a
  freed list and a NULL return.
