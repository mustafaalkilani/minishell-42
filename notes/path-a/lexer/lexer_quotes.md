# lexer_quotes.c

This is the heart of the lexer: the quote state machine and the two passes that
use it. `word_measure` finds where a word ends and how long it will be once the
quote delimiters are deleted; `word_fill` writes the surviving characters plus
the parallel `quotes[]` metadata array that the expander later depends on. Quote
*removal* happens here, at lex time, which is why every later stage works on
already-unquoted text and needs `quotes[]` to remember what the text used to
look like.

## Walkthrough

### `static int quote_step(char c, t_quote *state)`

```c
if (c == '\'' && *state != Q_DOUBLE)
{
    if (*state == Q_NONE)
        *state = Q_SINGLE;
    else
        *state = Q_NONE;
    return (1);
}
```

Advances the state machine by one character and returns 1 if that character was
a quote **delimiter** — meaning it is consumed by the lexer and never written to
the output. Returning 0 means "ordinary character, emit it".

The guard `*state != Q_DOUBLE` is what makes `"it's fine"` work: inside double
quotes a single quote is just data, so the function falls through to the `"`
test (which also fails, since `c` is not `"`) and returns 0. Without the guard
the apostrophe would open a single-quoted section and the word would run on
until the next apostrophe or the end of the line.

The toggle is written as "if currently Q_NONE, open; otherwise close". The
`else` branch can only ever be reached when `*state == Q_SINGLE`, because the
guard already excluded `Q_DOUBLE`. So there is no way to "close" a quote of the
wrong type.

```c
if (c == '"' && *state != Q_SINGLE)
{
    ...
}
return (0);
```

The mirror image for double quotes: inside single quotes a `"` is literal data,
which is how `echo 'say "hi"'` keeps its quote characters.

The function has no notion of nesting depth, and it does not need one: shell
quoting is not nestable. You are either outside quotes, inside single quotes, or
inside double quotes, and only the matching delimiter gets you out. A two-state
flag would not be enough, though — the state must distinguish `Q_SINGLE` from
`Q_DOUBLE` because the expander behaves differently in each (`$` expands in
double quotes, not in single).

### `int word_measure(const char *s, size_t i, size_t *end)`

```c
state = Q_NONE;
len = 0;
while (s[i])
{
    if (state == Q_NONE && (is_space(s[i]) || is_metachar(s[i])))
        break ;
    if (!quote_step(s[i], &state))
        len++;
    i++;
}
```

Pass one. The loop walks forward until the end of the string or the end of the
word, counting only the characters that `quote_step` did *not* claim as
delimiters.

The break condition is where word boundaries are actually decided, and the
`state == Q_NONE` prefix is the whole point: a space or a `|` inside quotes is
ordinary data. `echo "a | b"` is one word, `echo a | b` is three tokens. The
same test appears verbatim in `word_fill`, and the two must stay identical or the
second pass will write more bytes than the first pass counted.

The check happens *before* `quote_step`, which is correct: the quote characters
themselves are never spaces or metacharacters, so testing them first would be
harmless, but testing the break first keeps the two passes textually aligned.

```c
if (state != Q_NONE)
    return (-1);
*end = i;
return (len);
```

If the loop fell out because the string ran out while a quote was still open,
that is an unclosed quote. The return type is `int` rather than `size_t`
specifically so `-1` can carry that signal. Note the ordering: on the error path
`*end` is **never written**, so the caller must not read it. `read_word` obeys
this — it returns immediately when `len < 0`.

`*end` is the index of the first character after the word, i.e. the delimiter
(space, metacharacter, or the terminating NUL). The caller assigns it straight
to the scan cursor, so the top-level loop resumes exactly at the delimiter and
either skips it as whitespace or reads it as an operator.

A return of 0 with `*end > i` is not an error: that is `""` or `''`, a word with
no surviving characters. Bash keeps that as an empty argument, and `read_word`
happily mallocs 1 byte for the terminator.

### `void word_fill(const char *s, size_t i, char *value, char *quotes)`

```c
state = Q_NONE;
w = 0;
brk = 0;
while (s[i])
{
    if (state == Q_NONE && (is_space(s[i]) || is_metachar(s[i])))
        break ;
```

Pass two, over the same range with the same state machine and the same break
condition. There are two independent cursors here: `i` walks the raw input and
`w` walks the output arrays. They diverge by exactly the number of quote
delimiters dropped so far, which is why the output needed to be measured first.

There is no bounds check on `w`. The safety of every write rests entirely on
`word_measure` having counted the same characters this loop emits.

```c
    if (quote_step(s[i], &state))
        brk = Q_BREAK;
    else
    {
        value[w] = s[i];
        quotes[w] = (char)state | brk;
        w++;
        brk = 0;
    }
    i++;
}
```

This is the subtle part of the file. `brk` is a one-shot latch. When a delimiter
is consumed it is set, and the *next* character actually written inherits
`Q_BREAK` in bit 2 of its quotes byte; writing that character then clears the
latch. So `Q_BREAK` means "a quote delimiter was removed immediately before this
character".

Why that matters: quote removal happens here, so by the time the expander runs,
`$T"o"` and `$To` have the identical `value` string `$To`. Without a marker the
expander would look up a variable named `To`. With it, `quotes[2]` (the `o`) has
`Q_BREAK` set, and `var_name_len` in `expander_utils.c` stops the name scan at
any character carrying `Q_BREAK` — so it reads `T` and then appends a literal
`o`. The same byte drives `is_quote_prefix` in `expander_prefix.c`, which
recognises an unquoted `$` sitting directly in front of an opening quote
(bash's `$"..."` / `$'...'` forms) by testing `Q_BREAK` on the character after
the `$`.

`quotes[w] = (char)state | brk` records the state **after** `quote_step` has run
for the previous characters, so the first character inside `"..."` already sees
`Q_DOUBLE`: the opening quote was processed on the previous iteration and updated
`state` then. Likewise the character after a closing quote sees `Q_NONE`. The
`Q_MASK` (value 3) low bits hold the state and bit 2 holds the break, so the two
never collide.

One consequence to be aware of: a `brk` set by a **trailing** delimiter is
discarded, because the loop ends before any character is written. That is
harmless — `Q_BREAK` only exists to separate a character from the one before it,
and there is no following character to separate. For `"o"$T` the story is
different: the closing `"` sets the latch and the `$` receives
`Q_NONE | Q_BREAK`, which is exactly what `is_quote_prefix` and `var_name_len`
need.

```c
value[w] = '\0';
quotes[w] = '\0';
```

Both arrays are terminated at the same index, which is why `read_word` allocated
`len + 1` bytes for each. Keeping `quotes` NUL-terminated means the expander can
index it in lockstep with `value` and safely read `quotes[i + 1]` whenever it
can read `value[i + 1]` — `is_quote_prefix` and `var_name_len` both rely on that.

`w` must equal `len` here. Nothing asserts it; the guarantee is structural,
coming from the two loops being copies of each other.

## Things to be ready to explain

- **What exactly is stored in one byte of `quotes[]`?**
  The low two bits (`Q_MASK`) are the `t_quote` state the character was written
  under — `Q_NONE`, `Q_SINGLE` or `Q_DOUBLE`. Bit 2 (`Q_BREAK`, value 4) is set
  when a quote delimiter was removed immediately before this character.

- **Why do you need `Q_BREAK` at all?**
  Because quote removal happens in the lexer. After it, `$T"o"` and `$To` have
  the same text. `Q_BREAK` on the `o` is the only remaining evidence of the
  boundary, and `var_name_len` uses it to stop the variable name at `T`.

- **Why is a `"` inside `'...'` not a delimiter?**
  The state guards in `quote_step`. Only the matching quote type can close a
  quoted section, so `'say "hi"'` is a single word containing literal double
  quotes — matching bash exactly.

- **What guarantees `word_fill` does not overflow the buffer?**
  Only that its loop is character-for-character identical to `word_measure`'s:
  same start index, same break condition, same `quote_step` calls. There is no
  runtime bounds check, so the two functions must be edited together.

- **What does a return of 0 from `word_measure` mean?**
  A word with no surviving characters, i.e. `""` or `''`. It is not an error;
  the error signal is `-1`, used for an unclosed quote. `*end` is only written
  on the success path.

- **Why is the state a three-valued enum rather than a boolean "in quotes"?**
  The expander has to treat single and double quotes differently: `$VAR` expands
  inside double quotes but stays literal inside single quotes. The distinction
  must survive into `quotes[]`, so the state machine has to track it.
