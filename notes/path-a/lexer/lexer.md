# lexer.c

This is the entry point of the whole parsing half of the shell. It turns the raw
line returned by readline into a linked list of `t_token`, splitting on unquoted
whitespace and on the metacharacters `|`, `<`, `>`. It owns the top-level scan
loop and the word-reading logic; the quote state machine lives in
`lexer_quotes.c`, the operator recognition in `lexer_ops.c`, and the list
plumbing in `lexer_utils.c`. Its output feeds `expand()` and then `parse()`.

## Walkthrough

### `static int scan_had_quotes(const char *s, size_t start, size_t end)`

```c
while (start < end)
{
    if (s[start] == '\'' || s[start] == '"')
        return (1);
    start++;
}
return (0);
```

Scans a half-open range `[start, end)` of the **raw input** looking for any
quote character, and reports 1 if it finds one.

The reason this looks at the raw input rather than at the token's `value` is
that by the time a token exists, the quote characters have already been deleted
by `word_fill`. For most words you could reconstruct the fact from the `quotes[]`
array, but not for `""` or `''`: those produce a zero-length `value` and a
zero-length `quotes[]`, so nothing survives to prove a quote was ever typed.
Bash still keeps that as an argument (`echo ""` passes one empty argument, while
`echo $NOPE` passes none), so the parser needs the flag. This is why the function
takes the input string and two offsets instead of a token.

Two details worth noting. First, it does not care about quote *pairing* or which
kind of quote it is; a single hit anywhere in the word sets the flag. That is
safe because `word_measure` has already rejected unclosed quotes before this
runs, so any quote character found is necessarily part of a matched pair (or is
a literal quote nested inside the other kind, as in `"it's"` — which also
correctly counts as "this word had quotes"). Second, the range is half-open:
`end` is the index of the first character *after* the word, which is exactly
what the lexer's cursor holds after `read_word` returns.

### `static t_token *read_word(const char *s, size_t *i)`

```c
len = word_measure(s, *i, &end);
if (len < 0)
{
    shell_error(NULL, NULL, "syntax error: unclosed quote");
    return (NULL);
}
```

This is the measure-then-fill pattern. The first pass counts how many characters
will survive quote removal and where the word ends, so the second pass can write
into an exactly-sized buffer. The alternative — growing a buffer with realloc —
would need capacity tracking and would not fit inside Norminette's 25-line
functions.

`word_measure` returns `int` rather than `size_t` precisely so that `-1` can be
used as the "unclosed quote" sentinel. `shell_error(NULL, NULL, ...)` prints
`minishell: syntax error: unclosed quote`, because `shell_error` skips the `ctx`
and `arg` segments when they are NULL. Note that `*i` is **not** advanced on this
path and `end` is left uninitialised — that is fine only because the caller chain
aborts immediately on NULL and never looks at either again.

```c
value = malloc((size_t)len + 1);
quotes = ft_calloc((size_t)len + 1, 1);
if (!value || !quotes)
{
    free(value);
    free(quotes);
    return (NULL);
}
```

Both arrays are `len + 1` bytes: `word_fill` writes a `'\0'` terminator into
both, so `value` is a normal C string and `quotes` is index-parallel to it right
through the terminator. `value` is plain `malloc` because every byte of it gets
overwritten; `quotes` uses `ft_calloc` mostly as belt-and-braces, since
`word_fill` also writes every byte. The failure branch frees both
unconditionally — `free(NULL)` is defined as a no-op, so there is no need for two
separate tests. This branch returns NULL without printing anything, so an
allocation failure is reported to the user the same way a syntax error is,
which is a small imprecision but not a leak.

```c
word_fill(s, *i, value, quotes);
*i = end;
return (token_new(T_WORD, value, quotes));
```

The critical invariant of this file: `word_fill` is called with the **same**
starting index `*i` as `word_measure`, and the two functions use identical loop
break conditions (`state == Q_NONE && (is_space || is_metachar)`) and identical
quote handling. If those two loops ever disagree, `word_fill` writes past the end
of a buffer sized by `word_measure`. There is no bounds check inside `word_fill`
to catch it. Any change to one of them must be mirrored in the other.

`*i` is only advanced after the fill succeeds, and `token_new` takes ownership of
both allocations (it frees them itself if its own `malloc` fails), so this
function has no cleanup left to do.

A length of 0 with a non-empty consumed range is legitimate, not an error: that
is the empty word from `""` or `''`. `token_new` still builds a token for it,
and `had_quotes` is what later stops the parser from discarding it.

### `static t_token *next_token(const char *input, size_t *i)`

```c
if (is_metachar(input[*i]))
    return (read_operator(input, i));
start = *i;
tok = read_word(input, i);
if (tok)
    tok->had_quotes = scan_had_quotes(input, start, *i);
return (tok);
```

The one-token dispatcher. `is_metachar` (in `srcs/utils.c`) is true for `|`, `<`
and `>` only — the subject excludes `;`, `\`, `&&` and `||`, so there is nothing
else to recognise here. `read_operator` advances `*i` itself by the operator
length.

For words, the raw span is captured before and after the read so
`scan_had_quotes` can look at the original text. `had_quotes` is set here rather
than inside `read_word` because this is the only place that still holds the
starting offset after `read_word` has overwritten `*i`. The `if (tok)` guard
matters: on a syntax error or allocation failure `read_word` returns NULL and
dereferencing it would crash.

Operator tokens never get `had_quotes` set, but `token_new` already initialised
it to 0 and nothing reads it for non-word tokens except `redir_new`, which only
reads it from the *target* word.

### `t_token *lex(const char *input)`

```c
head = NULL;
i = 0;
while (input[i])
{
    while (input[i] && is_space(input[i]))
        i++;
    if (!input[i])
        break ;
```

The main scan. The inner loop skips runs of whitespace between tokens; the
`if (!input[i]) break` handles a line with trailing whitespace, where the skip
loop consumes the rest of the string. The outer `while (input[i])` condition is
then partly redundant with that break, but it costs nothing and makes the empty
input case terminate immediately.

Note that whitespace is only skipped *between* tokens. Whitespace inside a word
is either quoted (and consumed by `word_measure`'s state machine) or it ends the
word — the lexer never sees unquoted internal whitespace.

```c
    tok = next_token(input, &i);
    if (!tok)
    {
        free_tokens(head);
        return (NULL);
    }
    token_add_back(&head, tok);
}
return (head);
```

On any failure the whole partial list is freed before returning NULL. This is the
early return that also frees — forgetting it would leak every token read before
the bad quote.

`token_add_back` walks to the tail each time, so building n tokens is O(n²). For
a command line that is irrelevant, and it avoids carrying a tail pointer through
every call.

The return value is ambiguous by design: NULL means "blank line", "syntax error"
or "out of memory", and the caller cannot tell them apart. `main.c` resolves this
by calling `line_is_blank(line)` first and returning early, so by the time
`build_cmds` calls `lex`, a NULL can only be a failure and is mapped to exit
status 2 (`EXIT_MISUSE`). The error message itself has already been printed by
`read_word`.

## Things to be ready to explain

- **Why measure the word twice instead of building it as you go?**
  Because a single exact `malloc` is simpler than realloc-and-track-capacity and
  fits Norminette's function length. The cost is the invariant that both loops
  must break on exactly the same conditions; `word_fill` has no bounds check.

- **`echo ""` and `echo $NOPE` both end up with an empty `value`. How does the
  shell tell them apart?**
  `scan_had_quotes` looks at the raw input span and sets `had_quotes`. The parser
  keeps an empty word only if `had_quotes` is set, so `""` survives as an empty
  argument and an unset variable disappears.

- **What happens on `echo "hello`?**
  `word_measure` finishes the string with `state != Q_NONE` and returns -1;
  `read_word` prints `minishell: syntax error: unclosed quote` and returns NULL;
  `lex` frees the partial token list and returns NULL; `main` sets
  `last_status` to 2. Real bash would open a continuation prompt instead, which
  the subject does not require.

- **`lex` returns NULL for a blank line and for a syntax error. Isn't that a
  bug?**
  It is ambiguous, but `main` calls `line_is_blank` before `build_cmds`, so a
  blank line never reaches `lex`. The ambiguity is unreachable in practice.

- **Who owns the memory after `token_new` is called?**
  The token. `token_new` frees `value` and `quotes` itself on failure, so
  `read_word` never has to clean up after calling it. `free_tokens` frees all
  three pointers per node later.

- **Why is `had_quotes` set in `next_token` rather than in `read_word`?**
  `read_word` overwrites `*i` with the end of the word, so only `next_token`
  still holds both ends of the raw span the scan needs.
