# parser_redir.c

Everything to do with turning a redirection operator plus its target word into a
`t_redir` node hanging off the current command. `parse_step` in `parser.c`
delegates here for any token that is neither a word nor a pipe. The nodes built
here are consumed much later, in path-b: `redir_apply.c` opens the files, and the
heredoc reader fills in `heredoc_fd` before the pipeline forks.

## Walkthrough

### `static t_redir_type redir_type_of(t_token_type type)`

```c
if (type == T_REDIR_IN)
    return (R_IN);
if (type == T_APPEND)
    return (R_APPEND);
if (type == T_HEREDOC)
    return (R_HEREDOC);
return (R_OUT);
```

A plain mapping from the lexer's token enum to the parser's redirection enum. The
two enums are kept separate because they describe different layers: `t_token_type`
also has to represent words and pipes, which are not redirections at all, so a
`t_redir` should not be able to hold `T_PIPE`.

The final `return (R_OUT)` is a fall-through rather than an explicit
`T_REDIR_OUT` test. It is safe because of the caller chain: `parse_step` sends
only non-word, non-pipe tokens to `parse_redir_step`, and the lexer can only
produce six token types, four of which are redirections. Three are matched
explicitly, so the fall-through can only be `T_REDIR_OUT`.

### `static t_redir *redir_new(t_token *op, t_token *target)`

```c
redir = malloc(sizeof(t_redir));
if (!redir)
    return (NULL);
redir->filename = ft_strdup(target->value);
if (!redir->filename)
{
    free(redir);
    return (NULL);
}
```

Note the ordering: the node is allocated, then `filename` is duplicated, and if
that fails the node is freed before returning. This is the correct cleanup for a
partially-initialised struct — the other fields have not been touched yet, so
there is nothing else to release.

The `ft_strdup` is required for the same reason as in `parser.c`: `main` frees
the token list as soon as `parse` returns, so the redirection cannot borrow
`target->value`.

```c
redir->type = redir_type_of(op->type);
redir->quoted_delim = target->had_quotes;
redir->heredoc_fd = -1;
redir->next = NULL;
```

`quoted_delim` is only meaningful for heredocs. It encodes bash's rule that
`<<EOF` expands variables inside the body while `<<"EOF"` and `<<'EOF'` keep the
body completely literal. Since the lexer already stripped the quotes from the
delimiter, `had_quotes` is the only surviving evidence that they were typed —
the same flag that `parse_step` uses to keep empty quoted words. For the other
three redirection types the field is written but never read.

There is an important interaction with the expander here. `expand()` in
`expander.c` deliberately **skips** any word token whose predecessor is a
`T_HEREDOC`, so for a heredoc the `target->value` seen here is the raw
delimiter after quote removal (`<< "EOF"` gives `EOF`) and `had_quotes` still
reflects the original text. Bash likewise does not expand the delimiter itself.
For every other redirection type the target has already been expanded, so
`filename` is the final path.

`heredoc_fd = -1` is a sentinel meaning "no heredoc has been read yet". It has to
be set here, at construction, because `free_redirs` in `parser_utils.c` tests
`heredoc_fd >= 0` before calling `close()`. An uninitialised value there would
mean closing an arbitrary descriptor.

### `int cmd_add_redir(t_cmd *cmd, t_token *op, t_token *target)`

```c
redir = redir_new(op, target);
if (!redir)
    return (-1);
if (!cmd->redirs)
{
    cmd->redirs = redir;
    return (0);
}
cur = cmd->redirs;
while (cur->next)
    cur = cur->next;
cur->next = redir;
return (0);
```

Appends to the tail of the command's redirection list.

Appending rather than prepending is not cosmetic — redirection order is
semantically significant and must match the order typed. `cmd > a > b` opens both
files and leaves `b` connected to stdout because it is applied second;
`cmd < a < b` reads from `b`. If this prepended, both cases would silently
resolve to the wrong file. The same ordering is what makes
`cmd > out 2>&1`-style sequencing (to the extent this shell supports it) behave
predictably.

The early `return (0)` for the empty-list case exists because the tail walk needs
a non-NULL starting node. As in the token list, the walk makes this O(n) per
insertion, which is irrelevant for the handful of redirections a command can
realistically have.

`op` and `target` are not NULL-checked; `parse_redir_step` validates both before
calling.

### `int parse_redir_step(t_cmd *cmd, t_token **tokens)`

```c
op = *tokens;
target = op->next;
if (!target)
    return (syntax_error("newline"));
if (target->type != T_WORD)
    return (syntax_error(target->value));
```

Enforces the one grammatical rule for redirections: the operator must be followed
by a word.

The two error cases are distinguished because bash distinguishes them. `cat <`
with nothing after it reports `syntax error near unexpected token \`newline'`;
`cat < |` reports the offending token itself, `` `|' ``. The second case is why
`read_operator` in `lexer_ops.c` bothers to store the operator's literal text in
`value` — without it there would be nothing to print here.

```c
if (cmd_add_redir(cmd, op, target) < 0)
    return (-1);
*tokens = target->next;
return (0);
```

On success the cursor jumps past **two** tokens, the operator and its target.
That is why `tokens` is a double pointer here while `cmd` is passed by value: the
token cursor moves by two, but the command being built never changes — a
redirection belongs to the pipeline segment it appears in and never starts a new
one.

The `-1` return is passed straight up through `parse_step` to `parse`, which
frees the whole command list. Since `cmd_add_redir` only fails on allocation
failure, no message is printed on that path.

Two known divergences from bash worth knowing about, both stemming from the fact
that the target has already been through the expander:

- `> $NOPE` with `NOPE` unset produces a redirection with `filename` set to the
  empty string. Bash reports `ambiguous redirect`; here the `open` will simply
  fail with `No such file or directory`. It does not crash, but the message
  differs.
- `> $X` where `X` contains a space is split by the expander into two word
  tokens, so it becomes a redirection to the first field plus an extra argument
  from the second. Bash also calls this an ambiguous redirect. Again the
  behaviour is defined but not identical.

## Things to be ready to explain

- **Why does `quoted_delim` come from the target word rather than the operator?**
  Because it records whether the *delimiter* was quoted — `<<"EOF"` — which is
  bash's switch for disabling expansion inside the heredoc body. The operator
  itself can never be quoted.

- **Why is the heredoc delimiter not expanded like other redirection targets?**
  `expand()` skips the word following a `T_HEREDOC` token, matching bash, which
  does not perform expansion on the delimiter. So `<< $X` waits for the literal
  text `$X`.

- **Why append redirections instead of prepending?**
  Order is semantic. `> a > b` must apply `a` then `b`, so the last one wins.
  Prepending would silently reverse that.

- **What is `heredoc_fd = -1` for?**
  It is the "not yet opened" sentinel. `free_redirs` only calls `close()` when it
  is non-negative, so freeing a command list before the heredocs have been read
  is safe. Path-b fills it in before forking.

- **What does `cat < ` with nothing after it print, and why two different error
  strings?**
  `syntax error near unexpected token \`newline'` when the operator is the last
  token, and the offending token's own text when it is present but not a word.
  Both mirror bash exactly.

- **Why does `parse_redir_step` take `cmd` by value but `tokens` by address?**
  A redirection is attached to the command currently being built and never starts
  a new one, so the command pointer does not move. The token cursor advances by
  two, which requires writing through a pointer.
