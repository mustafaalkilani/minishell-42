# lexer_ops.c

The smallest file in the lexer: it recognises the five operator tokens the
subject requires — `|`, `<`, `>`, `<<` and `>>` — and builds a token for each.
It is reached from `next_token` whenever `is_metachar` says the current character
starts an operator, and it is the only place in the lexer that does not run the
quote state machine, because an operator cannot be quoted (a quoted `|` is part
of a word and never gets here).

## Walkthrough

### `static t_token_type op_type(const char *s, size_t i, size_t *len)`

```c
*len = 1;
if (s[i] == '|')
    return (T_PIPE);
```

`*len` is an out-parameter that tells the caller how far to advance the scan
cursor. Setting it to 1 up front means only the two-character cases have to touch
it, which keeps the function short enough for Norminette. `|` is tested first and
has no two-character form in this subject — `||` is explicitly out of scope, so
there is no ambiguity to resolve.

```c
if (s[i] == '<' && s[i + 1] == '<')
{
    *len = 2;
    return (T_HEREDOC);
}
if (s[i] == '>' && s[i + 1] == '>')
{
    *len = 2;
    return (T_APPEND);
}
```

The two-character operators are tested **before** the one-character ones. This
ordering is the whole reason the function is structured this way: if `>` were
tested first, `>>` would lex as two separate `T_REDIR_OUT` tokens and
`echo hi >> file` would become "redirect to nothing, then redirect to file",
which the parser would reject as a syntax error near `>`.

Reading `s[i + 1]` is safe without a length check. The caller only invokes this
when `is_metachar(s[i])` is true, which implies `s[i] != '\0'`, so `s[i + 1]` is
at worst the string's own terminator — inside the allocation, and never equal to
`<` or `>`. A lone `>` at the very end of the line therefore falls through
correctly.

```c
if (s[i] == '<')
    return (T_REDIR_IN);
return (T_REDIR_OUT);
```

The final `return` is a fall-through rather than an explicit `s[i] == '>'` test.
It is only correct because of the caller's precondition: `is_metachar` accepts
exactly `|`, `<` and `>`, and the first two have already returned. Written this
way it avoids a fourth `if` and an unreachable default return. The cost is that
if this function were ever called on some other character it would silently
claim it is `>` rather than failing loudly — worth knowing, though nothing in the
codebase can do that today.

Note what is absent: three-character sequences are not special-cased, so `<<<`
lexes as `T_HEREDOC` followed by `T_REDIR_IN`. Bash's here-string is out of
scope, and the parser will reject that pair anyway (a heredoc must be followed by
a word, and `<` is not one), producing
`syntax error near unexpected token \`<'`.

### `t_token *read_operator(const char *s, size_t *i)`

```c
type = op_type(s, *i, &len);
text = ft_substr(s, (unsigned int)*i, len);
if (!text)
    return (NULL);
*i += len;
return (token_new(type, text, NULL));
```

Classify, copy the literal text, advance, build the token.

The literal text is kept even though the `type` field already carries all the
information the parser needs to execute. Its only consumer is the error path:
`parse_redir_step` calls `syntax_error(target->value)` so that
`echo > |` prints ``syntax error near unexpected token `|'`` with the operator
the user actually typed. Without the stored text the parser would have to map
the enum back to a string.

The `if (!text) return (NULL)` before `*i += len` matters for ordering: on an
allocation failure the cursor is left untouched. That is not strictly necessary
since `lex` aborts the whole line on a NULL token, but it means the function has
no partial side effects.

`(unsigned int)*i` is a cast to match libft's `ft_substr(char const *s,
unsigned int start, size_t len)` signature. On a 64-bit build this truncates for
lines longer than 4 GB, which is not a realistic concern for a shell prompt but
is the kind of thing an evaluator may point at.

`token_new(type, text, NULL)` passes NULL for the `quotes` array. Operator tokens
have no per-character quote metadata because they were never subject to quoting.
That is safe throughout: `expand` in `expander.c` only processes tokens with
`type == T_WORD`, so nothing ever dereferences the NULL, and `free_tokens` calls
`free` on it, which is a no-op for NULL. `token_new` deliberately does not treat
a NULL `quotes` as a failure for exactly this reason.

Also note that `had_quotes` stays 0 on operator tokens (set by `token_new`).
`next_token` returns early for operators and never calls `scan_had_quotes`, which
is correct — the flag is only meaningful for words.

## Things to be ready to explain

- **Why are `<<` and `>>` tested before `<` and `>`?**
  Longest match first. Testing the single-character forms first would split `>>`
  into two `>` tokens and break append redirection entirely.

- **Is reading `s[i + 1]` safe?**
  Yes. The function is only called when `is_metachar(s[i])` is true, so `s[i]` is
  not the terminator and `s[i + 1]` is at worst the NUL — inside the buffer and
  not equal to `<` or `>`.

- **Why store the operator's literal text if the enum already says what it is?**
  For error messages. `syntax_error` quotes the token text back to the user, so
  `> |` reports `` `|' `` and not a numeric type.

- **Why is `quotes` NULL for operator tokens?**
  Operators cannot be quoted, so there is no per-character metadata to record.
  The expander skips non-word tokens, and `free_tokens` handles the NULL safely.

- **What does `<<<` do here?**
  It lexes as `T_HEREDOC` then `T_REDIR_IN`. Here-strings are not in the subject,
  and the parser rejects the pair with a syntax error because a heredoc operator
  must be followed by a word.

- **Why does the last branch return `T_REDIR_OUT` without checking for `>`?**
  Because the caller's precondition guarantees the character is `|`, `<` or `>`
  and the first two already returned. It saves a branch, at the price of failing
  silently if the precondition were ever broken.
