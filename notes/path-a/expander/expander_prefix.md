# expander_prefix.c

Two small, unrelated-looking rules that share a theme: both are about something at the *front* of a construct changing how it is read. `tilde_prefix` handles a leading `~` becoming `$HOME`, and it runs once before the main expansion loop, not inside it. `is_quote_prefix` recognises bash's `$"..."` / `$'...'` form, where an unquoted `$` sitting directly in front of an opening quote is a prefix on the quoted string rather than the start of a variable reference. Both are called from `expander.c` — `tilde_prefix` from `expand_masked`, `is_quote_prefix` from `append_var`.

Both functions are entirely driven by the quote metadata; neither can be written correctly against `value` alone.

## Walkthrough

### `size_t tilde_prefix(t_exp *e, const char *value, const char *quotes, int enabled)`

Called exactly once, from `expand_masked`, as the very first thing:

```c
i = tilde_prefix(&e, value, quotes, tilde);
start = i;
```

so `value` and `quotes` are always the start of the whole word and index 0 is the word's first character. The return value is the number of characters consumed — `1` if the tilde was swallowed, `0` otherwise — and `expand_masked` uses it both as the loop's starting index and as the start of the first literal run, so a consumed `~` is never re-emitted as text.

The fourth parameter is the caller's answer to "is tilde expansion allowed here at all?". `expand_masked` takes it as its own `tilde` parameter and does nothing with it but forward it. The two entry points differ: `expand` (command words) passes `1`, `expand_word` (heredoc bodies) passes `0`. Bash performs parameter expansion inside a heredoc body but never tilde expansion, so a body line `~/x` must stay `~/x`. Threading the decision down as a flag rather than, say, having `expand_word` skip the call keeps the single call site and puts the rule next to the code it governs.

```c
if (!enabled || value[0] != '~' || quotes[0] != Q_NONE)
    return (0);
```

Three guards. The `!enabled` test comes first and is a plain early-out: for a heredoc body nothing is consumed, `start` stays 0, and the leading `~` falls through to the literal-copy path like any other character.

The last guard is stricter than it first looks. This is `!=` against `Q_NONE` on the **whole byte**, not `(quotes[0] & Q_MASK) != Q_NONE`. So it rejects the tilde if it is quoted *and* if it merely carries `Q_BREAK`.

Rejecting a quoted tilde is obvious: `echo "~"` and `echo '~'` both print a literal `~` in bash.

Rejecting `Q_BREAK` is the interesting half. `Q_BREAK` on the first character means a quote delimiter was removed before it, i.e. the word began with something quoted — `""~/x` or `''~/x`. Bash prints `~/x` for those, because tilde expansion only applies when the tilde is the genuine first character of the word, and an empty quoted string still counts as preceding text. Using the plain `!=` here gets that case right for free. It is a nice example of `Q_BREAK` doing work outside variable names.

```c
if (value[1] && value[1] != '/')
    return (0);
```

The tilde expands only when it stands alone (`~`, so `value[1]` is NUL) or is immediately followed by a slash (`~/x`). Anything else is a `~user` form — `~root`, `~+`, `~-` — which bash resolves against the password database or the directory stack. This shell does none of that, so it correctly leaves them alone as literal text rather than half-implementing them. Note this means `~root` stays `~root`, which is what bash also does when the user does not exist, so the failure mode is at least a familiar one.

```c
home = env_get(e->sh->env, "HOME");
if (!home)
    return (0);
```

If `HOME` is unset the tilde stays literal and nothing is consumed. That matches bash's behaviour of leaving `~` alone when it cannot be resolved — though strictly, bash falls back to the passwd entry rather than to the literal, so with `unset HOME` bash still expands `~` while this shell prints `~`. A small, defensible deviation.

`env_get` returns a **borrowed** pointer into the live `t_env` list, which is why the next line strdups it.

```c
exp_append(e, ft_strdup(home), '0');
return (1);
```

`ft_strdup(home)` is mandatory, not stylistic: `exp_append` consumes and eventually frees whatever it is handed, so passing `home` directly would free the environment's own copy of `HOME` and leave a dangling pointer inside the `t_env` node. The fresh copy is owned by `exp_append` from the moment of the call, and there is nothing to free here.

The flag is `'0'`, so the expanded home directory is **not** splittable. That matches bash: tilde expansion is not subject to word splitting, so with `HOME="/a b"`, `cd ~` and `echo ~` treat the whole path as one word even though it contains a space. Getting this wrong would be an easy bug — the value came out of the environment, which superficially resembles the `$X` case, but the two are governed by different rules.

Returning `1` (not `strlen(home)`) is correct because the return value counts characters consumed **from the input word**, not characters produced. Only the `~` was consumed.

### `int is_quote_prefix(const char *quotes, size_t i)`

Called from `append_var` as `is_quote_prefix(quotes, *i)` where `*i` is the index of the `$`. It takes only the quotes array — the text is irrelevant, because everything this rule cares about is quoting structure that the lexer has already erased from `value`.

Its precondition is that `quotes[i + 1]` is readable. That holds because `expand_masked` only calls `append_var` after checking `value[i + 1]` is non-NUL, so index `i + 1` is a real character. Even at the terminator the read would be the NUL byte `0`, which fails every test below cleanly, so this is safe by construction and not just by luck.

```c
if ((quotes[i] & Q_MASK) != Q_NONE)
    return (0);
```

The `$` itself must be unquoted. Inside quotes the `$"..."` form does not exist — in bash, `"$'abc'"` is the six literal characters `$'abc'` because the inner quotes are just data. Masking with `Q_MASK` first is what lets an unquoted-but-break-carrying `$` (as in `""$"x"`) still qualify.

```c
return ((quotes[i + 1] & Q_BREAK) != 0);
```

That is the whole rest of the rule. The character right after the `$` must carry `Q_BREAK`, which means exactly one thing: a quote delimiter stood between the `$` and that character and the lexer deleted it. Since `Q_BREAK` is set only on the character that *follows* a removed delimiter, it already pins the quote to be immediately after the `$` — `$x` where `x` happens to be quoted later in the word leaves `quotes[i + 1]` unmarked and fails here.

An earlier version added a third test, `(quotes[i + 1] & Q_MASK) != Q_NONE`, on the theory that the removed delimiter had to be an *opening* quote and that the following character's own quote state was the way to tell an opener from a closer. That reasoning is wrong for empty quoted strings. In `$""x` the `""` opens and closes with nothing between, so no character is left in the `Q_DOUBLE` state; the `x` is `Q_NONE | Q_BREAK`. The third test therefore failed, the rule did not fire, and the shell printed `$x` where bash prints `x`.

Testing `Q_BREAK` alone is the correct rule, and the closer case it was supposed to guard against is already excluded by the first test. A closing quote's break lands on the character after the closer, and if that character is the `$`, then the break is on `quotes[i]`, not `quotes[i + 1]` — which is why `"x"$y` still does not fire (worked example 4). For the break to sit on `quotes[i + 1]` with the `$` at `i` unquoted, a delimiter must have been removed strictly between them, and the only delimiter that can appear there is an opener.

When both tests hold, `append_var` simply advances `*i` by 1 and appends nothing, so the `$` vanishes and the quoted text that follows is copied literally by the main loop. Bash's `$"..."` performs locale translation and `$'...'` decodes ANSI-C escapes; with no message catalogue and no escape decoder, dropping the `$` is the closest correct-looking behaviour. Be honest that `$'\n'` therefore produces a backslash and an `n` rather than a newline — the code comment says as much.

## Worked examples

Quote bytes shown as numbers: `0` = `Q_NONE`, `1` = `Q_SINGLE`, `2` = `Q_DOUBLE`, `+4` = `Q_BREAK`.

### 1. `~/x` with `HOME=/home/u`

```
value  =  ~   /   x
quotes =  0   0   0
```

`tilde_prefix` with `enabled = 1` (this is a command word, so `expand` passed `tilde = 1`): `value[0]` is `~`, `quotes[0]` is exactly `Q_NONE`, `value[1]` is `/`. `env_get` gives `/home/u`, which is strdup'd and appended with flag `'0'`. Returns 1.

Back in `expand_masked`, `i = start = 1`. The loop finds no `$` and runs to the end, then flushes `ft_substr(value, 1, 2)` = `"/x"` with `'0'`.

```
out  = / h o m e / u / x
mask = 0 0 0 0 0 0 0 0 0
```

One argument, `/home/u/x`. If `HOME` had been `/a b`, the mask would still be all `'0'` and the argument would remain the single string `/a b/x` — no splitting.

The same three bytes reaching `tilde_prefix` from `expand_word` — a heredoc body line `~/x` — take the `!enabled` early-out instead, return 0, and are copied through untouched, so the heredoc gets the literal `~/x`. Bash does the same.

### 2. `""~/x`

```
value  =  ~   /   x
quotes =  4   0   0        (the ~ carries Q_NONE|Q_BREAK from the deleted "")
```

`tilde_prefix`: `value[0]` is `~`, but `quotes[0]` is `4`, and `4 != Q_NONE`, so it returns 0 immediately. No expansion; `expand_masked` starts at `i = 0` and copies `~/x` literally. Bash agrees — `echo ""~/x` prints `~/x`. This is the case that justifies the strict `!=` rather than `& Q_MASK`.

### 3. `$"HOME"`

```
value  =  $   H   O   M   E
quotes =  0   6   2   2   2        (6 = Q_DOUBLE|Q_BREAK)
```

Note the closing quote's `Q_BREAK` is lost: `word_fill` writes it into `brk` but the word ends, and `quotes[len]` is set to `'\0'`. That is fine, nothing needs it.

`tilde_prefix` returns 0. At `i = 0` the loop sees a `$` that is not `Q_SINGLE` with a following character, flushes an empty literal run, and calls `append_var`:

- `quotes[0] & Q_MASK` is `Q_NONE`, so `flag = '1'` (which will turn out to be irrelevant).
- `is_quote_prefix(quotes, 0)`: `quotes[0] & Q_MASK` is `Q_NONE` — pass. `quotes[1] & Q_BREAK` is `6 & 4 = 4`, non-zero — true.
- So `*i += 1` and it returns having appended nothing.

`start = 1`, the loop copies `HOME` literally, and the tail flush appends `"HOME"` with `'0'`.

```
out  = H O M E
mask = 0 0 0 0
```

One argument, the literal text `HOME`. Bash prints `HOME` too. Without this rule, `append_named` would have been called, `var_name_len` would have rejected the name because `H` carries `Q_BREAK`, and the output would have been `$HOME` — the wrong answer.

### 4. `"x"$y` with `y=z` — the case `is_quote_prefix` must *not* fire on

```
value  =  x   $   y
quotes =  6   4   0        (x is Q_DOUBLE|Q_BREAK, $ is Q_NONE|Q_BREAK from the closing quote)
```

At `i = 1`, `append_var` runs `is_quote_prefix(quotes, 1)`: `quotes[1] & Q_MASK` is `Q_NONE` — pass. `quotes[2] & Q_BREAK` is `0 & 4 = 0` — **fail**, returns 0. Good: the break here is on the `$` itself (from the closing quote before it), not on the character after it, and this rule only cares about an opening quote *following* the `$`.

`append_named` then runs normally: `var_name_len("y", {0,0})` gives 1, and `y` expands to `z` with flag `'1'`.

```
out  = x   z
mask = 0   1
```

One argument, `xz`.

### 5. `$""x` — the empty quoted prefix

The lexer strips both quote delimiters, and the `""` contributes no characters between them, so the word is two bytes long:

```
value  =  $   x
quotes =  0   4        (x is Q_NONE|Q_BREAK — the "" opened and closed, and the closer's break landed on the x)
```

The `x` is genuinely unquoted — it was never inside the `""` — but it still carries `Q_BREAK`, because a delimiter was removed immediately in front of it.

`tilde_prefix` returns 0. At `i = 0` the loop sees a `$` whose quote state is not `Q_SINGLE` and which has a following character, flushes an empty literal run, and calls `append_var`:

- `quotes[0] & Q_MASK` is `Q_NONE`, so `flag = '1'` (irrelevant, nothing is appended under it).
- `is_quote_prefix(quotes, 0)`: `quotes[0] & Q_MASK` is `Q_NONE` — pass. `quotes[1] & Q_BREAK` is `4 & 4 = 4`, non-zero — **true**.
- So `*i += 1` and it returns having appended nothing.

`start = 1`, the loop copies `x` literally, and the tail flush appends `"x"` with `'0'`.

```
out  = x
mask = 0
```

One argument, `x`. Bash prints `x` too.

This is the input that forced the rule to be simplified. The old three-test version asked whether `quotes[1]` was itself inside quotes, and here it is not — `0 != 0` is false — so the rule did not fire, `append_named` ran, `var_name_len` returned 0 because `x` carries `Q_BREAK`, and the shell printed the literal `$x`. An empty quoted string leaves no character in the `Q_DOUBLE` state, so no test on the following character's quote state can see it. `Q_BREAK` on its own is the thing that survives, and it says precisely what the rule needs to know.

## Things to be ready to explain

- **Why is `tilde_prefix` called before the loop instead of inside it?**
  Because tilde expansion is a prefix rule: bash only expands a `~` that is the first character of a word (and, in assignments, after `=` or `:`, which this shell does not implement). Calling it once at index 0 makes it structurally impossible for `a~b` or `x/~/y` to expand, without needing any "am I at the start?" test inside the loop.

- **Why `quotes[0] != Q_NONE` and not `(quotes[0] & Q_MASK) != Q_NONE`?**
  Deliberate. The strict form also rejects a tilde that merely carries `Q_BREAK`, which means the word started with a quoted section (`""~/x`). Bash does not tilde-expand there, so the strict test is the correct one. Everywhere else in the expander the `& Q_MASK` form is used, so this is the one place where the difference is load-bearing.

- **What is the `enabled` parameter for?**
  It is how the heredoc path turns tilde expansion off. Bash expands parameters inside a heredoc body but never tildes, so `expand_word` passes `0` and `expand` passes `1`, and `expand_masked` forwards the flag without inspecting it. This used to be a real bug: a body line `~/x` came out as `/home/user/x`. The alternative — testing "am I a heredoc?" inside `tilde_prefix` — would have meant giving the expander knowledge of its callers, which the flag avoids.

- **Why is the tilde result marked `'0'` when `$HOME` would be marked `'1'`?**
  Because bash does not field-split a tilde expansion. With `HOME="/a b"`, `echo ~` prints one argument while `echo $HOME` prints two. The flag is a statement about which bash expansion produced the text, not about where the text physically came from.

- **Why does `is_quote_prefix` take only the quotes array and not the text?**
  Because the thing it detects — a quote delimiter between the `$` and the next character — no longer exists in the text. The lexer stripped it. `Q_BREAK` on `quotes[i + 1]` is the only surviving evidence, so the text has nothing to contribute.

- **Why is testing `Q_BREAK` on the next character enough? Doesn't a closing quote set it too?**
  It does, but a closing quote sets it on the character *after the closer*, and for that to be `quotes[i + 1]` the delimiter would have to sit between the `$` and the next character — where a closer cannot be, since the `$` at `i` has already been checked to be unquoted. The closer case puts the break on the `$` itself instead, which is `quotes[i]`, and that is why `"x"$y` does not fire. So given an unquoted `$`, `Q_BREAK` on the following character can only have come from an opener.

- **Why was the "is the next character inside quotes" test removed?**
  Because it broke `$""x`. An empty quoted pair leaves no character in the `Q_SINGLE` / `Q_DOUBLE` state, so the following `x` is `Q_NONE | Q_BREAK` and the test rejected a case bash accepts — this shell printed `$x`, bash prints `x`. The test was trying to distinguish an opening delimiter from a closing one, but the first test (`$` unquoted) already does that job, so it was redundant as well as wrong.

- **Which inputs were re-checked after that change?**
  `$"HOME"` prints `HOME`, `$T"o"` expands `T` and appends `o`, `"$"USER` prints `$USER`, `"~"` prints `~`, `"x"$y` expands `y`, and `$""x` now prints `x`. All match bash.

- **What does `$'...'` actually do here, and how does that differ from bash?**
  The `$` is dropped and the single-quoted text is taken literally. Bash decodes ANSI-C escapes, so `$'\n'` is a newline in bash but a backslash followed by `n` here. Likewise `$"..."` performs locale translation in bash and is a plain pass-through here. Both are outside the subject's requirements, and the code comment states the limitation.

- **Anything else that deviates?**
  `~user` is never resolved (left literal), and `~` with `HOME` unset stays literal where bash falls back to the passwd database. Neither is required by the subject.
