# expander_split.c

The last step of EXPAND: turning one expanded word into one or more argv entries. `expand()` hands it a finished `t_exp` — the expanded text `out` plus the parallel `mask` — and this file cuts `out` into fields at whitespace, but **only** at whitespace whose mask byte is `'1'`, i.e. whitespace that arrived through an unquoted expansion. The first field overwrites the original token's value in place; any further fields are spliced into the token list as fresh `T_WORD` nodes immediately after it. This is what implements bash's word splitting without ever running a splitter over the user's literal text.

It runs after the mask has been built and before PARSE, and it is the only consumer of the mask.

## Walkthrough

### `static int is_split(t_exp *e, size_t i)`

```c
return (e->mask[i] == '1' && is_space(e->out[i]));
```

The entire distinction between `$X` and `"$X"` is these two conjuncts.

- `e->mask[i] == '1'` means the character at `i` came from an *unquoted* expansion. Literal characters and characters from a quoted expansion are `'0'`.
- `is_space(e->out[i])` (from `srcs/utils.c`: space, tab, newline, vertical tab, form feed, carriage return) means it is actually a separator character.

Both are required. Whitespace that the user typed literally cannot split, because the lexer already ended the word at any unquoted whitespace — so any literal whitespace still present inside a word must have been quoted, and quoted whitespace never splits. And a non-whitespace character from an expansion obviously does not split. So `"$X"` with `X="a b"` yields mask `000` and stays one argument, while unquoted `$X` yields mask `111` and becomes two.

Note this is not a full IFS implementation. Bash splits on the characters in `$IFS`, which defaults to space/tab/newline; this uses a fixed `is_space` set and ignores a user-set `IFS`. That is the normal simplification for the 42 subject, which does not require IFS.

Precondition: `e->mask` must be non-NULL and the same length as `e->out`. `exp_append` maintains that, with the one out-of-memory exception noted at the end.

### `static int field_next(t_exp *e, size_t *i, size_t *start)`

A cursor-style iterator: each call either reports one field and leaves `*i` just past it, or reports that the text is exhausted.

```c
while (e->out[*i] && is_split(e, *i))
    (*i)++;
if (!e->out[*i])
    return (0);
```

Skip any run of separators first. This is why leading separators produce no empty field, and why a run of several spaces produces one boundary rather than several empty fields — bash collapses runs of IFS whitespace. If skipping reaches the terminator, there is nothing left, so it returns 0 and **`*start` is left untouched**. That is safe only because the caller never reads `start` after a 0 return, which is worth knowing since `start` is otherwise uninitialised on the first call.

```c
*start = *i;
while (e->out[*i] && !is_split(e, *i))
    (*i)++;
return (1);
```

Record the field start, then run to the next separator or the end. On return the field is the half-open range `[*start, *i)`, so its length is exactly `*i - *start` and the caller can slice it with `ft_substr` without any off-by-one adjustment. `*i` is left *on* the separator (or the NUL), which is exactly where the next call's skip loop wants to begin — the cursor is never rewound and never double-advanced.

Because the skip loop runs first on every call, trailing separators are consumed by the final call, which then returns 0. So `$X` with `X="a b "` gives two fields, not three.

### `static int token_set(t_token *tok, char *value)`

```c
if (!value)
    return (-1);
free(tok->value);
tok->value = value;
return (0);
```

Replaces a token's text and **takes ownership of `value`**. Callers pass an `ft_substr` or `ft_strdup` result directly and never free it; on the NULL (allocation failed) path nothing has been freed and nothing has been modified, so the token is still consistent and `free_tokens` will still work.

The important thing to notice is what it does **not** do: it frees `tok->value` but leaves `tok->quotes` alone. After this call the quotes array is stale — it still describes the *pre*-expansion text and its length no longer matches `tok->value`. That is deliberate and safe only because nothing reads `quotes` after EXPAND: `parse_step` looks at `tok->value` and `tok->had_quotes` only, and `free_tokens` just calls `free(tokens->quotes)` without caring about its length. Be ready to state that invariant explicitly, because a reader who assumes `value` and `quotes` are always parallel would consider this a bug.

### `static int insert_after(t_token **tok, char *value)`

```c
quotes = NULL;
if (value)
    quotes = ft_calloc(ft_strlen(value) + 1, 1);
if (!value || !quotes)
{
    free(value);
    free(quotes);
    return (-1);
}
```

A zeroed quotes array of the right length is allocated for the new token even though expansion is already finished and nothing will ever read it. Two reasons: `free_tokens` unconditionally calls `free(tokens->quotes)` and it must have something valid (well, `free(NULL)` is legal, so this is really about keeping the token type's contract intact rather than avoiding a crash), and a zero-filled array is the semantically honest value — every character of a post-expansion field is unquoted `Q_NONE` with no break. The failure branch frees both partial allocations, so nothing leaks.

```c
node = token_new(T_WORD, value, quotes);
if (!node)
    return (-1);
```

`token_new` takes ownership of both `value` and `quotes` and frees them itself on every one of its own failure paths (see `lexer_utils.c`), so there is no leak here despite the bare `return (-1)` — this is the one place where *not* freeing is correct, and it is worth saying so out loud because it looks like a leak at a glance.

`token_new` also sets `had_quotes = 0` on the new node. That is harmless: `had_quotes` only matters for deciding whether an *empty* value survives, and every field produced by `field_next` is non-empty by construction (it always contains at least one non-separator character). So a split-off field can never be dropped by the parser.

```c
node->next = (*tok)->next;
(*tok)->next = node;
*tok = node;
return (0);
```

Standard singly-linked splice: the new node inherits the current node's successor and becomes the current node's successor. Then `*tok = node` moves the caller's cursor onto the node just created.

That last line is the subtle one. `*tok` is `&tokens` from `expand()`'s own loop variable, so advancing it here mutates `expand()`'s cursor. The consequences: on return, `expand()` continues from the **last field produced**, so the newly created tokens are never re-expanded. That is required for correctness, not just efficiency — if `X='$Y'` then `echo $X` must print the literal `$Y`, and bash does not re-expand the result of an expansion. Re-walking the new nodes would also mean re-walking them with an all-zero quotes array, which would happily expand any `$` inside them.

### `int split_token(t_token **tok, t_exp *e)`

```c
i = 0;
if (!field_next(e, &i, &start))
    return (token_set(*tok, ft_strdup("")));
```

No field at all — the word expanded to nothing, or to nothing but splittable whitespace. Rather than deleting the token, its value is set to the empty string and it is left in the list. The decision of whether an empty argument survives is deferred to the parser, which checks `tok->value[0] || tok->had_quotes` in `parse_step`. That is how `echo $NOPE` (`had_quotes == 0`, dropped, echo gets no arguments) is distinguished from `echo "$NOPE"` (`had_quotes == 1`, kept, echo prints a blank line). Splitting cannot make that decision itself because by this point the quotes are long gone; `had_quotes` was recorded by the lexer scanning the *raw* input.

```c
if (token_set(*tok, ft_substr(e->out, (unsigned int)start, i - start)) < 0)
    return (-1);
```

The first field reuses the existing token. This matters for the common case — a word that does not split at all is the overwhelming majority — since it means no allocation of a new node and, importantly, the original token keeps its `had_quotes`, its position in the list and its identity as (for example) a redirection target.

`(unsigned int)start` is a cast to match `ft_substr`'s prototype, and `i - start` is the exact field length because `field_next` leaves `i` one past the last field character.

```c
while (field_next(e, &i, &start))
{
    if (insert_after(tok, ft_substr(e->out, (unsigned int)start,
                i - start)) < 0)
        return (-1);
}
return (0);
```

Every subsequent field is spliced in after the previous one. Because `insert_after` advances `*tok`, each new node is appended after the last one produced rather than all being inserted at the same point — so the fields come out in order rather than reversed.

`e->out` and `e->mask` are only read here; `split_token` never takes ownership of them. `expand()` frees both afterwards on both the success and failure paths. On a `-1` return the token list is left in a valid, freeable state — partially split, but every node well-formed — and `expand()` propagates the failure so the whole line is abandoned and `free_tokens` cleans up.

## Worked examples

### 1. `echo $X` with `X="  a  b  "`

From `expand_masked`, the mask is all `'1'` because the `$` was unquoted:

```
index  0   1   2   3   4   5   6   7
out   ' ' ' '  a  ' ' ' '  b  ' ' ' '
mask   1   1   1   1   1   1   1   1
```

- `field_next` #1: skip `i=0,1` (both split), `start = 2`, advance to `i = 3` (a split). Returns 1. Field = `out[2..3)` = `"a"`. `token_set` replaces the token's value with `"a"`.
- `field_next` #2: skip `i=3,4`, `start = 5`, advance to `i = 6`. Returns 1. Field = `"b"`. `insert_after` creates a new `T_WORD` node holding `"b"` and moves the cursor onto it.
- `field_next` #3: skip `i=6,7`, hits the NUL. Returns 0. Loop ends.

Result: two argv entries, `a` and `b`. Trailing whitespace produced no empty third field.

### 2. `echo "$X"` with the same `X`

Same text, different mask — `append_var` set `flag = '0'` because the `$` was `Q_DOUBLE`:

```
out   ' ' ' '  a  ' ' ' '  b  ' ' ' '
mask   0   0   0   0   0   0   0   0
```

`is_split` is false everywhere. `field_next` #1 skips nothing, sets `start = 0`, runs to the terminator `i = 8`, returns 1. One field, the whole string including both leading and trailing spaces. `field_next` #2 immediately sees the NUL and returns 0. One argv entry, `"  a  b  "`. This is the pair of examples that demonstrates the mask's entire purpose.

### 3. `echo a$X"b c" ` with `X=" q "`

The word token after lexing is `value = "a$Xb c"` with the `b` carrying `Q_BREAK|Q_DOUBLE`. After expansion:

```
index  0   1   2   3   4   5   6
out    a  ' '  q  ' '  b  ' '  c
mask   0   1   1   1   0   0   0
        \_ from unquoted $X _/  \_ literal, quoted _/
```

- `field_next` #1: `i=0` is not a split (mask `'0'`), so `start = 0`; advance until `i = 1`, which *is* a split. Field = `"a"`.
- `field_next` #2: skip `i=1`; `start = 2`; advance — `i=3` is a split. Field = `"q"`.
- `field_next` #3: skip `i=3`; `start = 4`; advance — `i=5` is a space but its mask is `'0'`, so not a split; continue to the NUL at `i=7`. Field = `"b c"`.
- `field_next` #4: returns 0.

Three arguments: `a`, `q`, `b c`. The space at index 5 was typed by the user inside double quotes, so it is preserved inside the third argument, while the spaces at 1 and 3 came out of `X` unquoted and split.

### 4. `echo $NOPE` and `echo "$NOPE"` with `NOPE` unset

Both produce `out = ""` and `mask = ""`. `field_next` immediately hits the NUL and returns 0, so both take the early branch and get `token_set(*tok, ft_strdup(""))`. The tokens are *identical* at this point — the only difference is `had_quotes`, which the lexer set to 0 and 1 respectively by scanning the raw input for quote characters. The parser then drops the first and keeps the second. So `echo $NOPE` prints one blank line (echo with no arguments) and `echo "$NOPE"` also prints a blank line, but for a different reason — and `echo $NOPE x` gives one argument while `echo "$NOPE" x` gives two, which is the observable difference.

## Things to be ready to explain

- **Why check the mask at all — why not just split `out` on whitespace?**
  Because by the time we have `out`, the quoting information is gone. `"$X"` and `$X` with `X="a b"` produce byte-identical text; only the mask distinguishes them. Splitting the raw text would break `echo "$X"` (one argument in bash) and would also split literal quoted spaces like `echo "a b"`.

- **Why does splitting only ever happen on expanded text, never on typed text?**
  The lexer already ended the word at any unquoted whitespace, so whitespace typed by the user either terminated the word (and never reaches here) or was quoted (and gets mask `'0'`). Field splitting in bash is by definition a post-expansion step, and this structure enforces it rather than merely implementing it.

- **Why does `insert_after` move `*tok` forward?**
  So `expand()`'s loop resumes past the newly created nodes and never re-expands them. Bash does not re-expand the result of an expansion — `X='$Y'; echo $X` must print `$Y` literally — and the new tokens carry an all-zero quotes array, so a second pass would treat every `$` in them as live.

- **Why is the token's `quotes` array not updated to match the new value?**
  Because nothing reads it after EXPAND. `token_set` frees only `value`, leaving `quotes` stale and of a mismatched length. The parser uses `value` and `had_quotes`; `free_tokens` frees `quotes` without inspecting it. It is a deliberate but real invariant break, and it is the answer to "is `value` always parallel to `quotes`?" — yes before expansion, no after.

- **Why keep an empty token instead of deleting it here?**
  Because splitting cannot tell `""` from `$NOPE` — both are empty by now. The distinction lives in `had_quotes`, which the lexer computed from the raw input, and the parser applies it in `parse_step` with `if (tok->value[0] || tok->had_quotes)`. Deleting the node here would also mean re-linking around it, which needs the predecessor pointer that `split_token` does not have.

- **Doesn't `insert_after` leak `value` and `quotes` when `token_new` fails?**
  No. `token_new` takes ownership of both and frees them on all of its own failure paths, so the bare `return (-1)` is correct. This is the one place in the file where not freeing is deliberate.

- **What breaks if a redirection target splits into several fields?**
  `> $FILE` with `FILE="a b"` produces two tokens, so the parser takes `a` as the filename and `b` becomes a stray command argument. Bash reports `ambiguous redirect` and runs nothing. This shell does not detect that case; it is a known deviation, and the place to fix it would be `parse_redir_step`, not here.

- **Fragility worth admitting.** `is_split` dereferences `e->mask` without a NULL check. `expand()` only verifies `e.out`, and `exp_append` can leave `mask` NULL while `out` is valid if the mask's `pad_new` fails but the text's `ft_strjoin` succeeds. Under real allocation failure that is a NULL dereference.
