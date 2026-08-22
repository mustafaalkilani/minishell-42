# expander.c

This is the core of the EXPAND stage, the third step of the pipeline `read line -> LEX -> EXPAND -> PARSE -> EXECUTE`. It walks every `T_WORD` token produced by the lexer, copies literal runs through unchanged and substitutes `$VAR` / `$?` where the quote metadata says a `$` is live, building both the expanded text and the parallel split mask. It then hands the result to `split_token` so one word can become several argv entries. It also exposes `expand_word`, the single entry point path-b uses to expand heredoc bodies.

The file relies on three things it does not implement itself: `var_name_len` / `var_lookup` / `exp_append` (in `expander_utils.c`), `tilde_prefix` / `is_quote_prefix` (in `expander_prefix.c`) and `split_token` (in `expander_split.c`).

## Walkthrough

### `static size_t append_named(t_exp *e, const char *value, const char *quotes, char flag)`

The contract stated in the comment above it is the thing to memorise: **`value` and `quotes` point at the `$` itself, not at the character after it.** The caller rebases them (`value + *i`, `quotes + *i`) precisely so that index 0 is the dollar sign. Everything inside the function is written against that assumption, and the return value is a count measured from the `$`.

```c
len = var_name_len(value + 1, quotes + 1);
```

`value + 1` is the first candidate name character and `quotes + 1` is its quote byte. Passing both rebased pointers is what lets `var_name_len` do two independent jobs with plain index-0 tests: check that the first character is a valid identifier start, and check that the first character does not carry `Q_BREAK`. That second check is the whole reason `quotes + 1` is passed at all — it is what makes `"$"USER` produce a literal `$` followed by `USER` instead of looking up `USER`, because the `U` inherited a `Q_BREAK` from the closing double quote that the lexer deleted.

```c
if (len == 0)
{
    exp_append(e, ft_strdup("$"), '0');
    return (1);
}
```

A `$` that is not followed by a valid identifier stays literal, which is bash behaviour for `echo $`, `echo $ x`, `echo $-`, `echo $%`. Two details worth defending:

- The flag is hardcoded `'0'`, not the `flag` parameter. A literal `$` did not come from an expansion, so it must never be allowed to split the word. Passing `flag` here would be a bug in a case like `X` unset and input `$ $X` — although in practice a literal `$` is not whitespace so `is_split` would reject it anyway. Hardcoding `'0'` is the honest expression of the invariant regardless.
- It returns `1`, i.e. the `$` has been *consumed and emitted*. The caller therefore sets `start = i` after the `$`, so the literal-copy path does not emit the `$` a second time. Returning a non-zero value in every branch is also what guarantees forward progress in the `expand_masked` loop; `append_named` can never return 0, so the shell cannot hang on a malformed word.

```c
name = ft_substr(value, 1, (size_t)len);
if (!name)
    exp_append(e, NULL, flag);
else
{
    exp_append(e, var_lookup(name, e->sh), flag);
    free(name);
}
return (1 + (size_t)len);
```

`ft_substr(value, 1, len)` again uses the rebased pointer, so it slices out the name *without* the `$`. On allocation failure it calls `exp_append(e, NULL, flag)` deliberately: `exp_append` poisons both `out` and `mask` to NULL when handed NULL, which is how the whole file propagates out-of-memory without an error branch on every line. The caller only has to test `e.out` once at the very end.

Ownership: `name` is a temporary owned here and freed here. `var_lookup` returns a **freshly allocated** string every time (it strdups, never returns the env node's own pointer), and `exp_append` consumes it — so there is no free of the lookup result here and there must not be one. The return `1 + len` is `$` plus the name length, again measured from the `$`.

### `static void append_var(t_exp *e, const char *value, size_t *i, const char *quotes)`

This is the dispatcher for one `$`-expansion, and it is the only function that advances `*i` past one.

```c
flag = '0';
if ((quotes[*i] & Q_MASK) == Q_NONE)
    flag = '1';
```

The split flag is decided by the quote state of **the `$` itself**, not of the produced text (the produced text has no quote metadata at all — it came out of the environment). This is the single line that implements "`$X` field-splits, `"$X"` does not". `& Q_MASK` strips the `Q_BREAK` bit first, because a `$` can legitimately be both unquoted and preceded by a removed quote (e.g. `""$X`), and that must still count as unquoted.

```c
if (is_quote_prefix(quotes, *i))
{
    *i += 1;
    return ;
}
```

The `$"..."` / `$'...'` rule. If the `$` is unquoted and a quote delimiter was removed directly after it — which is exactly what `Q_BREAK` on the next character records — the `$` is a bash locale/ANSI-C prefix. Since this shell has no message catalogue and no escape decoding, the `$` is simply dropped: nothing is appended, and `*i` advances by exactly 1 so the loop resumes on the quoted text and copies it literally. This is what makes `$"HOME"` print `HOME` rather than the value of `$HOME`. Note the test is about the delimiter that *was there*, not about the quote state of what follows: in `$""x` the empty pair leaves the `x` unquoted, and the rule still fires, which is what bash does.

Note the ordering: this test comes **before** the `$?` test and before `append_named`. That is required, because otherwise `$"..."` would fall into `append_named`, where `var_name_len` would reject the name anyway and emit a literal `$` — the wrong answer.

Note also the precondition: `is_quote_prefix` reads `quotes[i + 1]`. That is only safe because the caller has already checked `value[i + 1]` is non-NUL before calling, so index `i + 1` is a real character (or at worst the NUL, which reads as 0 and fails every test cleanly).

```c
if (value[*i + 1] == '?')
{
    *i += 2;
    return (exp_append(e, ft_itoa(e->sh->last_status), flag));
}
```

`$?` is handled here rather than through the environment because it is shell state, not an env variable — it never appears in `env` output. `*i += 2` consumes `$` and `?`. `ft_itoa` allocates and `exp_append` consumes it. The `return (exp_append(...))` on a `void` function is a Norminette-friendly idiom for "do this and return", not a value return.

```c
*i += append_named(e, value + *i, quotes + *i, flag);
```

The rebase described above. `*i` is advanced by exactly what `append_named` reports it consumed, keeping `i` and the two parallel arrays in lockstep.

### `static t_exp expand_masked(const char *value, const char *quotes, t_shell *sh, int tilde)`

The main loop, and the function that owns the `t_exp`. The `tilde` parameter is pure pass-through: it is never read here, only handed to `tilde_prefix`. It exists because the two public entry points want different rules — `expand` passes `1`, `expand_word` passes `0`.

```c
e.out = ft_strdup("");
e.mask = ft_strdup("");
e.sh = sh;
```

Both start as owned empty strings rather than NULL, so `exp_append` / `join_free` have something to append to from the first call and never need a "first chunk" special case. If either strdup fails, the NULL poisons everything downstream and the caller catches it.

```c
i = tilde_prefix(&e, value, quotes, tilde);
start = i;
```

Tilde expansion is a *prefix* rule: it is tried exactly once, before the loop, and only at index 0. Calling it here rather than inside the loop is what stops `a~b` or `~` in the middle of a word from expanding. It returns the number of characters it consumed — `1` if it swallowed a leading `~`, `0` otherwise — and `start` is set to that so the literal-copy run begins after the consumed `~`.

When `tilde` is 0 the function returns 0 unconditionally, so `i` and `start` both stay 0 and the leading `~` is simply copied as literal text by the loop. That is the whole mechanism by which heredoc bodies escape tilde expansion; nothing else in the file changes behaviour between the two entry points.

```c
while (value[i])
{
    if (value[i] == '$' && (quotes[i] & Q_MASK) != Q_SINGLE
        && value[i + 1])
```

Three conditions for a live `$`:
- it is a `$`;
- its quote state is not `Q_SINGLE` — inside single quotes a `$` is plain data, which is the one absolute rule of bash quoting. Note the test is `!= Q_SINGLE` rather than `== Q_NONE`, because a `$` in double quotes **does** expand;
- there is at least one more character. A trailing `$` is literal (`echo $` prints `$`), and this check is also what makes reading `value[i + 1]` and `quotes[i + 1]` safe in `append_var` and `is_quote_prefix`.

```c
    exp_append(&e, ft_substr(value, (unsigned int)start, i - start), '0');
    append_var(&e, value, &i, quotes);
    start = i;
}
else
    i++;
```

The literal run `[start, i)` is flushed first, always with flag `'0'`. Literal text is never splittable — and it does not need to be, because the lexer already terminated the word at any unquoted whitespace, so a literal run physically cannot contain a splitting space. Then `append_var` emits the expansion and advances `i` itself, and `start` is reset to the new `i`. The `else i++` branch is the only other way `i` moves; every path through the `if` advances `i` by at least 1 via `append_var`, so the loop always terminates.

```c
exp_append(&e, ft_substr(value, (unsigned int)start, i - start), '0');
return (e);
```

The final literal tail. When the word ended on an expansion this is a zero-length substr, which `ft_substr` returns as an owned empty string — harmless, `exp_append` just appends nothing to both sides. The struct is returned **by value**; the two heap pointers inside it now belong to the caller.

### `char *expand_word(const char *value, const char *quotes, t_shell *sh)`

```c
e = expand_masked(value, quotes, sh, 0);
free(e.mask);
return (e.out);
```

The public helper declared in `minishell.h` and used by path-b's heredoc reader. A heredoc body is expanded but **never** field-split — bash performs parameter expansion inside a heredoc body but no word splitting and no globbing — so the mask has no consumer and is freed immediately. The caller owns the returned `e.out` and must free it; `redir_heredoc.c` does.

The `0` in the last argument is the tilde switch, and it is load-bearing. The heredoc caller passes an all-zero `quotes` array of the line's length, meaning "every character is unquoted", which is right for `$` handling but would otherwise make every body line starting with `~` look like a textbook tilde expansion to `tilde_prefix`. Passing `tilde = 0` is what keeps a body line `~/x` literal, matching bash.

### `int expand(t_token *tokens, t_shell *sh)`

```c
prev = NULL;
while (tokens)
{
    if (tokens->type == T_WORD && (!prev || prev->type != T_HEREDOC))
```

Only word tokens are expanded — operators carry no quote metadata (`token_new` is called with `NULL` quotes for them) so touching them would dereference NULL. The `prev->type != T_HEREDOC` guard skips the token immediately after a `<<`: that token is the heredoc *delimiter*, and bash does not expand the delimiter. Whether the delimiter was quoted is what decides if the body gets expanded, and the parser records that separately by copying `target->had_quotes` into `redir->quoted_delim` in `parser_redir.c`.

```c
    e = expand_masked(tokens->value, tokens->quotes, sh, 1);
    if (!e.out || split_token(&tokens, &e) < 0)
    {
        free(e.out);
        free(e.mask);
        return (-1);
    }
    free(e.out);
    free(e.mask);
}
prev = tokens;
tokens = tokens->next;
```

The `1` is the tilde switch: these are command words, where a leading `~` does expand. It is the only difference between this call and the one in `expand_word`.

`expand()` is the owner of the `t_exp` for the duration of one token: it frees `out` and `mask` on both the success and failure paths, so `split_token` never takes ownership of them — it only ever `ft_substr`s copies out of `e.out`. The `!e.out` test is the single place the poison-propagation scheme is cashed in for the whole word.

The `&tokens` in `split_token(&tokens, &e)` is important. `split_token` splices any extra field tokens in after the current one and leaves `*tok` pointing at the **last** field it created. So when `prev = tokens; tokens = tokens->next;` runs, the loop resumes *past* everything that was just produced. Two consequences: the freshly created field tokens are never re-expanded (no double expansion of a `$` that happened to appear inside a variable's value — which is correct bash behaviour), and `prev` is correctly a `T_WORD`.

## Worked examples

Throughout, quote bytes are shown as numbers: `0` = `Q_NONE`, `2` = `Q_DOUBLE`, `1` = `Q_SINGLE`, `+4` = `Q_BREAK`.

### 1. `echo "$X"y` with `X="a b"`

The lexer strips the quotes, so the word token is:

```
value  =  $   X   y
quotes =  6   2   4        (6 = Q_DOUBLE|Q_BREAK, 2 = Q_DOUBLE, 4 = Q_NONE|Q_BREAK)
```

`tilde_prefix` returns 0 (`value[0]` is not `~`). `i = start = 0`.

- `i=0`: `$`, `quotes[0] & Q_MASK == Q_DOUBLE` which is not `Q_SINGLE`, and `value[1]` exists. Flush `ft_substr(value, 0, 0)` = `""` with `'0'`.
- `append_var`: `quotes[0] & Q_MASK` is `Q_DOUBLE`, so `flag = '0'`. `is_quote_prefix` fails on its first test (the `$` is not `Q_NONE`). `value[1]` is `X`, not `?`. So `append_named(e, value+0, quotes+0, '0')`.
- `var_name_len("Xy", {2,4,0})`: `X` is alpha, `quotes[0] & Q_BREAK` is `2 & 4 = 0` so no break, `len = 1`. Next, `y` is alnum but `quotes[1] & Q_BREAK` is `4 & 4 = 4`, truthy, so the loop stops. **`len = 1`.** The closing quote between `X` and `y` is what stopped it.
- `name = "X"`, lookup gives `"a b"`, appended with `'0'`. Returns `1 + 1 = 2`, so `i = 2`, `start = 2`.
- `i=2`: `y` is not `$`, `i = 3`, loop ends. Flush `ft_substr(value, 2, 1)` = `"y"` with `'0'`.

```
out  = a   ' '  b   y
mask = 0    0   0   0
```

No `'1'` anywhere, so `split_token` finds one field: the single argument `a b y`. Matches bash.

### 2. `echo $T"o"` with `T=x`

```
value  =  $   T   o
quotes =  0   0   6        (the o carries Q_DOUBLE|Q_BREAK)
```

- `i=0`: live `$`. Flush `""`. `quotes[0] & Q_MASK == Q_NONE` so `flag = '1'`.
- `is_quote_prefix(quotes, 0)`: `quotes[0] & Q_MASK` is `Q_NONE`, good; but `quotes[1] & Q_BREAK` is `0 & 4 = 0`, so it returns 0. Correct — the quote is after the `T`, not before it.
- `append_named`: `var_name_len("To", {0,6,0})`. `T` alpha, no break on index 0, `len = 1`. `o` is alnum but `quotes[1] & Q_BREAK = 4` — stop. `len = 1`, name `"T"`, value `"x"` appended with `'1'`. Returns 2, `i = 2`, `start = 2`.
- Tail flush: `"o"` with `'0'`.

```
out  = x   o
mask = 1   0
```

One argument `xo`. Without `Q_BREAK` this would have looked up a variable named `To` and printed nothing — this example is the whole justification for the `Q_BREAK` bit.

### 3. `echo $X` with `X="  a  b  "`

```
value  =  $   X
quotes =  0   0
```

`append_var` sets `flag = '1'`, `var_name_len` reads the whole `X`, and the value is appended entirely under `'1'`:

```
out  = ' ' ' '  a  ' ' ' '  b  ' ' ' '
mask =  1   1   1   1   1   1   1   1
```

`split_token` then skips the leading run of splittable spaces, emits `a`, skips the middle run, emits `b`, and the trailing run yields no further field. Two arguments. If the input had been `"$X"` the mask would have been all `'0'` and the result would be one argument with all the spaces preserved.

### 4. `echo a$NOPE"b"` with `NOPE` unset

```
value  =  a   $   N   O   P   E   b
quotes =  0   0   0   0   0   0   6
```

- `i=0`: `a` is not `$`, `i = 1`.
- `i=1`: live `$`. Flush `ft_substr(value, 0, 1)` = `"a"` with `'0'`. `flag = '1'`. `var_name_len("NOPEb", {0,0,0,0,6,0})` stops before `b` because of `Q_BREAK`, giving `len = 4`, name `"NOPE"`.
- `var_lookup` finds nothing and returns `ft_strdup("")`. `exp_append` with an empty chunk: `pad_new(0, '1')` is an empty pad, so **nothing is added to either side**. Returns `1 + 4 = 5`, `i = 6`, `start = 6`.
- Tail flush: `"b"` with `'0'`.

```
out  = a   b
mask = 0   0
```

One argument `ab`. Compare with `echo $NOPE` alone: `out` would be `""`, `field_next` finds no field, `split_token` sets the token value to `""`, and since `had_quotes` is 0 the parser drops the argument entirely — `echo` runs with no arguments. With `echo "$NOPE"` the lexer sets `had_quotes = 1`, so the same empty value is kept and echo prints a blank line.

## Things to be ready to explain

- **Why does `append_named` receive pointers to the `$` rather than to the name?**
  So that a single rebase (`value + *i`, `quotes + *i`) inside `append_var` covers the whole helper, and so that `ft_substr(value, 1, len)` and `var_name_len(value + 1, quotes + 1)` are both expressed as "one past the dollar" instead of the caller having to pass an extra offset. The return value is then naturally "characters consumed including the `$`", which is exactly what `*i +=` needs.

- **Why is the condition `!= Q_SINGLE` and not `== Q_NONE`?**
  Because `$` expands inside double quotes and only stops expanding inside single quotes. `== Q_NONE` would break `echo "$HOME"`. The separate `Q_NONE` test in `append_var` is a *different* question — not "does it expand" but "may the result be split" — which is why the two tests exist independently.

- **Where is the `$` re-emitted when the name is invalid, and why doesn't it get printed twice?**
  `append_named` emits it itself with `exp_append(e, ft_strdup("$"), '0')` and reports 1 character consumed. The caller then sets `start = i` *after* the `$`, so the next literal flush begins past it. If `append_named` had returned 0 instead, the `$` would both be duplicated and the loop would never advance.

- **Why is the heredoc delimiter skipped in `expand()` instead of being handled by the parser?**
  Bash does not expand the delimiter word, and expanding it would also destroy the information the parser needs — `had_quotes` on the delimiter token is copied into `redir->quoted_delim` and decides whether the *body* is expanded. Expanding the delimiter would be wrong twice over.

- **Who owns `e.out` and `e.mask`?**
  `expand_masked` allocates both and transfers them to its caller by returning the struct. `expand()` frees both on every path. `expand_word()` frees only `mask` and passes `out` to its caller (the heredoc writer), which frees it. `split_token` never takes ownership; it only copies substrings out.

- **What is the known deviation around `$1`?**
  `var_name_len` rejects a digit as a name start, so `echo $1abc` prints `$1abc` literally. Bash treats `$1` as an unset positional parameter and prints `abc`. This shell has no positional parameters, so this is a deliberate simplification, but note the comment in `expander_utils.c` describes this case ambiguously and the literal-`$` behaviour is what the code actually does. Similarly `echo $""` prints `$` here where bash prints an empty line.

- **Does a heredoc body get tilde-expanded, and should it?**
  It does not, and it should not. This was a real bug and it is now fixed. `expand_word` used to run the full `expand_masked`, `tilde_prefix` included, and the heredoc reader passes an all-zero quotes array, so a body line beginning with `~` or `~/` had `value[0] == '~'` and `quotes[0] == Q_NONE` and was replaced by `$HOME`: `<<EOF` followed by a line `~/x` printed `/home/user/x` where bash prints `~/x`. Bash performs parameter expansion, command substitution and arithmetic inside a heredoc body — never tilde expansion.
  The fix was to thread the decision down as a flag rather than teach `tilde_prefix` about its callers. `tilde_prefix` takes a fourth parameter, `int enabled`, and returns 0 immediately when it is false; `expand_masked` takes a matching `int tilde` and does nothing with it but forward it; `expand` passes `1` and `expand_word` passes `0`. With `tilde = 0` nothing is consumed, `start` stays 0, and the `~` falls through to the ordinary literal-copy path, so the body line comes out untouched. Command words are unaffected.

- **Malloc-failure fragility worth admitting.**
  `exp_append` can leave `e.out` non-NULL while `e.mask` is NULL, if the text join succeeds but `pad_new` fails. `expand()` only tests `!e.out`, so `split_token` would then dereference a NULL `mask` in `is_split`. This only happens under real allocation failure, but the honest answer is that the poison scheme is one-sided: a NULL `out` is always caught, a NULL `mask` alone is not.
