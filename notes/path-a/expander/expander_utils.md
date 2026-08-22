# expander_utils.c

The support layer under `expander.c`. It answers two questions the main loop asks — "how long is the variable name starting here?" (`var_name_len`) and "what is this variable worth?" (`var_lookup`) — and provides the three-function allocation discipline (`join_free`, `pad_new`, `exp_append`) that grows the expanded text and its split mask together so they can never drift out of sync. Nothing here knows about tokens or fields; it is pure string and metadata plumbing used only during EXPAND.

The invariant this file exists to guarantee is: **`strlen(e->out) == strlen(e->mask)` at all times, or both are NULL.**

## Walkthrough

### `int var_name_len(const char *s, const char *quotes)`

Called from `append_named` as `var_name_len(value + 1, quotes + 1)`, so inside this function **index 0 is the first character after the `$`**, not the `$` itself. Every index below is relative to that.

```c
if (!ft_isalpha(*s) && *s != '_')
    return (0);
```

The POSIX identifier rule: a name is `[A-Za-z_][A-Za-z0-9_]*`. Returning 0 is the caller's signal to leave the `$` as a literal character, which is what bash does for `echo $`, `echo $ `, `echo $%`, `echo $-`. Note this also rejects a leading digit, so `$1` is left literal — see the deviation note at the bottom.

```c
if (quotes[0] & Q_BREAK)
    return (0);
```

This is the subtle one. `Q_BREAK` on the first name character means a quote delimiter was removed immediately before it. If a quote stood between the `$` and the would-be name, then in the original input the `$` and the name were not adjacent, so there is no variable reference at all. This is what makes `"$"USER` print the literal string `$USER`: the `U` carries `Q_NONE|Q_BREAK` from the closing double quote, `var_name_len` returns 0, and the caller emits a literal `$` and then copies `USER` as text. Without this test the shell would look up `USER` and print the username, which is wrong.

Note the test is `& Q_BREAK`, not `==`, because the byte also carries the quote state in its low bits and both pieces of information are live simultaneously.

```c
len = 1;
while (s[len] && (ft_isalnum(s[len]) || s[len] == '_')
    && !(quotes[len] & Q_BREAK))
    len++;
return (len);
```

The name continues over alphanumerics and underscores, and **stops at the first `Q_BREAK`**. That second termination condition is the other half of the quote-metadata trick: in `$T"o"` the lexer deleted the opening quote before `o`, leaving `o` marked `Q_BREAK`, so the name ends at `T`. The shell looks up `T` and appends a literal `o`, exactly as bash does. Without it the surviving characters would read as the single word `To` and the shell would look up a variable that does not exist.

The function returns a length only; it allocates nothing and the caller does the `ft_substr`.

### `char *var_lookup(const char *name, t_shell *sh)`

```c
if (!ft_strncmp(name, "?", 2))
    return (ft_itoa(sh->last_status));
```

`$?` is shell state, not an environment variable — it never appears in the `env` list, so looking it up through `env_get` would always miss and return the empty string. The `ft_strncmp(..., 2)` compares two bytes, i.e. `?` plus the NUL, which is a full-string equality test rather than a prefix test.

Be honest about this branch at defence: **it is currently unreachable from the normal path.** `append_var` intercepts `$?` itself before ever calling `append_named`, and `var_name_len` would never accept `?` as a name anyway (it is not alpha and not `_`), so `var_lookup` is never called with `"?"`. It is defensive duplication rather than live code. It does no harm and keeps `var_lookup` correct as a standalone helper, but claiming it is load-bearing would be inaccurate.

```c
value = env_get(sh->env, name);
if (!value)
    return (ft_strdup(""));
return (ft_strdup(value));
```

Two things to be precise about.

First, **ownership**. `env_get` returns `node->value` — a *borrowed* pointer into the live `t_env` list. It must never be handed to `exp_append`, which frees whatever it is given. The `ft_strdup` is therefore mandatory, not cosmetic: without it, expanding `$PATH` would free the environment's own copy of PATH and leave a dangling pointer in the env list. `var_lookup` always returns a freshly allocated string that the caller (`append_named`, via `exp_append`) consumes.

Second, an **unset variable expands to the empty string, not to an error and not to NULL.** That is bash behaviour without `set -u`, and returning `ft_strdup("")` rather than NULL is what stops an unset variable from poisoning the whole expansion as if it were an allocation failure. The distinction between "expanded to nothing" and "allocation failed" is carried entirely by this line. The fact that the empty result later causes the argument to disappear (`echo $NOPE` runs echo with no arguments) is decided further downstream, by `split_token` producing no field and the parser checking `had_quotes`.

### `char *join_free(char *dst, char *src)`

```c
if (!dst || !src)
{
    free(dst);
    free(src);
    return (NULL);
}
```

The poison rule. If either side is already NULL — meaning some earlier allocation failed — the other is freed and NULL is returned, so the failure propagates forward instead of leaking. `free(NULL)` is defined to be a no-op, so no null checks are needed around the frees. This is why the whole expander can be written without an error branch after every append: one NULL anywhere makes every subsequent join NULL, and `expand()` tests `e.out` exactly once at the end.

```c
joined = ft_strjoin(dst, src);
free(dst);
free(src);
return (joined);
```

**Both arguments are consumed unconditionally.** Callers must not free what they pass in, and must not reuse the pointers afterwards. `ft_strjoin` allocates a new buffer, so `joined` is independent of both. If `ft_strjoin` itself fails, `joined` is NULL and both inputs have still been freed — no leak, and the NULL poisons the next join.

### `char *pad_new(size_t n, char flag)`

```c
pad = malloc(n + 1);
if (!pad)
    return (NULL);
ft_memset(pad, flag, n);
pad[n] = '\0';
return (pad);
```

Builds a run of `n` identical mask bytes plus a terminator. The mask is deliberately a NUL-terminated **string** of `'0'` and `'1'` characters rather than a raw byte array, so it can be grown with the same `ft_strjoin`-based `join_free` as the text and its length always matches `ft_strlen(out)`. That is the whole reason the mask uses the ASCII characters `'0'`/`'1'` and not the integers 0/1 — the value 0 would terminate the string.

`n == 0` is a valid call: it mallocs one byte and returns `""`, which appends nothing. That path is hit constantly, every time an unset variable or an empty literal run is appended.

### `void exp_append(t_exp *e, char *chunk, char flag)`

The only function anything outside this file should use to grow a `t_exp`.

```c
pad = NULL;
if (chunk)
    pad = pad_new(ft_strlen(chunk), flag);
e->out = join_free(e->out, chunk);
e->mask = join_free(e->mask, pad);
```

`pad_new(ft_strlen(chunk), flag)` is the line that enforces the central invariant: the mask grows by exactly as many bytes as the text, with every one of those bytes carrying the same flag. Since a chunk is always either one whole literal run or one whole expansion result, a uniform flag per chunk is sufficient — the flag is a property of *where the text came from*, not of individual characters.

`chunk` is consumed by `join_free` whether or not the append succeeds, so callers hand over ownership at the call site and never free afterwards. That is why `append_named` writes `exp_append(e, var_lookup(name, e->sh), flag)` with no temporary and no free of the lookup result, and why `tilde_prefix` writes `exp_append(e, ft_strdup(home), '0')` rather than passing `home` directly.

Passing `chunk == NULL` is a legitimate call: `pad` stays NULL, and both joins poison. `append_named` uses this on `ft_substr` failure to abort the whole expansion with one line.

A fragility to acknowledge rather than defend: if `chunk` is valid but `pad_new` fails, `e->out` is joined successfully while `e->mask` becomes NULL. The two are then out of sync in the one direction the caller does not check, since `expand()` only tests `e.out`. `split_token` would go on to read `e->mask[i]` from a NULL pointer. This can only occur under genuine allocation failure, but it is a real hole in the otherwise symmetric poison scheme.

## Worked examples

### 1. `var_name_len` on the three quote-boundary cases

Given `X=hello` and the lexer output for each input (quote bytes as numbers, `4` = `Q_BREAK`):

| input | `value` | `quotes` | call | result |
|---|---|---|---|---|
| `$X` | `$X` | `0 0` | `var_name_len("X", {0,0})` | 1 — name `X` |
| `$X"y"` | `$Xy` | `0 0 6` | `var_name_len("Xy", {0,6,0})` | 1 — stops at `y` because `6 & 4` is set |
| `"$"X` | `$X` | `6 4` | `var_name_len("X", {4,0})` | 0 — index 0 already carries `Q_BREAK` |

The third row is the case people get wrong. The `$` is inside the quotes and `X` is outside; the only surviving evidence is the `Q_BREAK` on `X`, and the early `if (quotes[0] & Q_BREAK) return (0);` is what catches it. The result is the literal text `$X`.

### 2. `exp_append` building `echo $X"y"` with `X="a b"`

Starting from `out = ""`, `mask = ""`:

1. Literal run before the `$` is empty: `exp_append(e, "", '0')`. `pad_new(0, '0')` gives `""`. State unchanged: `out = ""`, `mask = ""`.
2. Expansion of `X`: `exp_append(e, "a b", '1')` — note `var_lookup` returned a fresh strdup of the env value. `pad_new(3, '1')` gives `"111"`.
   `out = "a b"`, `mask = "111"`.
3. Literal tail `y`: `exp_append(e, "y", '0')`. `pad_new(1, '0')` gives `"0"`.
   `out = "a by"`, `mask = "1110"`.

Lengths match at every step. Downstream, `is_split` finds `mask[1] == '1'` and `out[1] == ' '`, so this splits into `a` and `by` — which is what bash does.

### 3. An unset variable adjacent to a literal: `echo pre$NOPEpost`

`var_name_len("NOPEpost", all-zero quotes)` runs to the end of the word and returns 8, so the name is `NOPEpost` — there is no `Q_BREAK` to stop it, and that is correct: unquoted `pre$NOPEpost` really does reference a variable called `NOPEpost` in bash too. `var_lookup` misses and returns `ft_strdup("")`, `pad_new(0, '1')` returns `""`, and the append is a no-op on both arrays.

```
out  = p r e
mask = 0 0 0
```

One argument `pre`. If the user had written `pre$NOPE"post"` instead, the `Q_BREAK` on `p` would cut the name at `NOPE` and the result would be `prepost`.

### 4. Allocation-failure propagation

Suppose `ft_substr` fails mid-word and `append_named` calls `exp_append(e, NULL, '1')`.

- `chunk` is NULL, so `pad` stays NULL.
- `join_free(e->out, NULL)` frees the accumulated text and returns NULL.
- `join_free(e->mask, NULL)` frees the accumulated mask and returns NULL.

Every subsequent `exp_append` now hits `if (!dst || !src)` and frees its incoming chunk immediately, so the rest of the word is processed without leaking anything. `expand()` sees `!e.out`, frees both (both already NULL — harmless), and returns -1.

## Things to be ready to explain

- **Why does `var_name_len` need the `quotes` array at all? Isn't a name just characters?**
  Because the lexer has already deleted the quote characters. Once `$T"o"` has become the four bytes `$To`, the only thing distinguishing it from `$To` is the `Q_BREAK` bit on `o`. Without the quotes array the two inputs would be indistinguishable and the shell would give the wrong answer for one of them.

- **Why two `Q_BREAK` checks instead of one?**
  They cover different inputs. The check on `quotes[0]` handles a boundary *before* the name (`"$"USER` — no reference at all, literal `$`). The check inside the loop handles a boundary *inside* the name (`$T"o"` — a real reference that ends early). Merging them is not possible because the first returns 0 and the second returns a positive length.

- **Why does `var_lookup` strdup instead of returning `env_get`'s pointer directly?**
  `env_get` returns a borrowed pointer into the live environment list. `exp_append` frees everything it is given, so returning the borrowed pointer would free the environment's own string and leave a dangling pointer in `t_env`. The strdup transfers a private copy that `exp_append` is allowed to consume.

- **Why is the mask an ASCII string of `'0'`/`'1'` rather than a byte array of 0/1?**
  So it can be grown with the same NUL-terminated string machinery (`ft_strjoin` via `join_free`) as the text, which is what mechanically guarantees the two stay the same length. With raw 0/1 bytes the value 0 would terminate the string and `ft_strjoin` would truncate the mask at the first non-splittable character.

- **What happens if I call `exp_append` and then free the chunk myself?**
  Double free. `join_free` consumes both of its arguments unconditionally, including on the error path. Every call site in the expander hands over a freshly allocated expression and never keeps the pointer.

- **Is the `$?` branch in `var_lookup` reachable?**
  Not from the current expander. `append_var` handles `$?` before `append_named` runs, and `var_name_len` would reject `?` as a name start regardless. It is defensive code that makes `var_lookup` correct in isolation; it is not what implements `$?`.
