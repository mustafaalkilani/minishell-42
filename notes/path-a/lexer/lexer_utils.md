# lexer_utils.c

The token list plumbing: allocate a token, append one to the tail, and free the
whole list. Nothing here knows anything about shell syntax; it exists so that
`lexer.c`, `lexer_ops.c` and the expander's field splitter can all build tokens
without repeating allocation and cleanup code. `free_tokens` is also part of the
public API in `minishell.h`, because `main.c` frees the token list once `parse`
has copied everything it needs out of it.

## Walkthrough

### `t_token *token_new(t_token_type type, char *value, char *quotes)`

```c
if (!value)
{
    free(quotes);
    return (NULL);
}
```

The contract of this function is that it **takes ownership** of both `value` and
`quotes` the moment it is called, including on the paths where it fails. That is
why the very first thing it does is handle a NULL `value`: a caller that passed
the result of a failed `ft_substr` or `ft_strdup` straight in gets NULL back and,
importantly, does not leak the `quotes` array that was allocated alongside it.
The caller never needs a cleanup branch of its own.

Note that it deliberately does **not** test `quotes` for NULL. A NULL `quotes` is
legal: `read_operator` in `lexer_ops.c` passes NULL because operator tokens have
no per-character quote metadata. That is precisely why `value` and not `quotes`
is used as the failure sentinel.

```c
tok = malloc(sizeof(t_token));
if (!tok)
{
    free(value);
    free(quotes);
    return (NULL);
}
```

Same ownership rule applied to the second failure point. If the node allocation
fails, both incoming buffers are released here rather than leaked back to a
caller who has already conceptually handed them over.

```c
tok->type = type;
tok->value = value;
tok->quotes = quotes;
tok->had_quotes = 0;
tok->next = NULL;
```

Straight field initialisation, with no copying — the token stores the pointers it
was given. Two fields are worth calling out:

`had_quotes = 0` is the safe default. `lexer.c` overwrites it immediately for
words it read from the input, but the expander's `insert_after` (in
`expander_split.c`) also calls `token_new` for the extra fields produced by
splitting `$X` into several arguments, and for those 0 is the correct value:
those fields came out of an *unquoted* expansion, so an empty one should be
dropped, not kept.

`next = NULL` matters because `token_add_back` walks until it finds a NULL
`next`; an uninitialised pointer here would send that walk into garbage.

### `void token_add_back(t_token **head, t_token *tok)`

```c
if (!*head)
{
    *head = tok;
    return ;
}
cur = *head;
while (cur->next)
    cur = cur->next;
cur->next = tok;
```

Classic append-to-tail on a singly linked list. The double pointer exists only
for the empty-list case: assigning to `*head` is the only way to make the
caller's `head` variable point at the first node. Once the list is non-empty the
head never changes, so the rest of the function works through a local cursor.

The tail walk makes appending O(n) and building the list O(n²). This is a
conscious trade: a command line has a handful of tokens, and the alternative
(threading a tail pointer through `lex` and `next_token`) adds a parameter to
every function for no measurable gain.

Note that `tok` is assumed non-NULL — `lex` checks for NULL before calling this,
so the check is not duplicated here.

### `void free_tokens(t_token *tokens)`

```c
while (tokens)
{
    next = tokens->next;
    free(tokens->value);
    free(tokens->quotes);
    free(tokens);
    tokens = next;
}
```

The `next = tokens->next` before the frees is the standard list-destruction
ordering: reading a field out of a node after `free(tokens)` would be a
use-after-free. Every node owns exactly three allocations — itself, `value` and
`quotes` — and all three are released.

`free(tokens->quotes)` is unconditional, which is correct because operator
tokens carry `quotes == NULL` and `free(NULL)` is a defined no-op. The same
applies to any token whose `value` is somehow NULL, though `token_new` makes
that unreachable.

This function is called from three places with different intents: `lex` uses it
to discard a partially built list after a syntax error, `main`'s `build_cmds`
uses it to discard the list after a failed `expand`, and `build_cmds` also calls
it on the success path once `parse` has finished. That last one is why the parser
must `ft_strdup` every string it keeps — see `parser.c`.

## Things to be ready to explain

- **Why does `token_new` free its own arguments on failure?**
  Because it takes ownership immediately. That single convention removes cleanup
  branches from every call site, and it means there is exactly one place to audit
  for token leaks.

- **Why is `value` the NULL sentinel and not `quotes`?**
  A NULL `quotes` is valid data: operator tokens have no quote metadata. Only a
  NULL `value` can mean "the allocation you were about to hand me failed".

- **Isn't the O(n²) append a problem?**
  Not for a command line, which has at most a few dozen tokens. Avoiding a tail
  pointer keeps `lex` and `next_token` inside Norminette's parameter and length
  limits.

- **Why is `next` saved before the node is freed in `free_tokens`?**
  Reading `tokens->next` after `free(tokens)` is a use-after-free. The saved
  local is the only way to keep walking the list.

- **Where does `had_quotes` get its value if `token_new` always zeroes it?**
  `next_token` in `lexer.c` sets it from `scan_had_quotes` for words read from
  the input. Tokens created later by field splitting keep 0, which is the correct
  answer for a field produced by an unquoted expansion.
