# env_export.c

The bridge between the internal linked list and the outside world. It converts
the `t_env` list into the NULL-terminated `char **` that `execve` demands
(`env_to_envp`), validates identifiers on behalf of the `export` and `unset`
builtins (`env_key_is_valid`), and implements the append assignment
`export A+=B` (`env_append`). Two of the three functions exist purely to honour
the rule that a variable declared without a value is visible to `export` but is
**not** passed to child processes.

## Walkthrough

### `static char *make_pair(t_env *node)`

```c
head = ft_strjoin(node->key, "=");
if (!head)
	return (NULL);
pair = ft_strjoin(head, node->value);
free(head);
return (pair);
```
Rebuilds one `"KEY=VALUE"` string, which is the only format `execve` accepts —
the kernel does not know about our list, it just receives an array of flat
strings.

Two joins because `ft_strjoin` takes exactly two arguments; the intermediate
`head` is a temporary and is freed unconditionally on the line after its last
use, whether the second join succeeded or not. If the second join fails, `pair`
is NULL, `head` is still freed, and NULL propagates — no leak on the error
path, which is the thing to point at.

The returned string is **owned by the caller**. `env_to_envp` stores it into
the array; the array is later released by `free_split`, defined in
`path-b/executor/exec_path.c`, which frees every element and then the array
itself.

`node->value` is dereferenced without a NULL check. That is safe only because
the sole caller guards with `if (env->value)` before calling. Passing a
NULL-value node here would hand `ft_strjoin` a NULL second argument. The
precondition is real but it is enforced at the call site rather than here, so
be honest that this function is not defensive on its own — it is `static` and
has exactly one caller, which is what makes that acceptable.

### `static int env_export_count(t_env *env)`

```c
n = 0;
while (env)
{
	if (env->value)
		n++;
	env = env->next;
}
return (n);
```
Counts only the nodes that will actually be exported, so the array can be
allocated at the right size in one shot rather than grown.

The `if (env->value)` is the rule stated at the top of the file: a variable
declared without a value (`export X`, represented as `value == NULL`) is listed
by the `export` builtin but is **not** placed in a child's environment. Bash
behaves exactly this way — `export X` then `env | grep X` prints nothing, while
`export` on its own shows `declare -x X`. Counting and copying must use the
same predicate, which is why this test appears twice, once here and once in the
loop below.

Getting the count wrong in either direction is fatal: too small and the copy
loop writes past the end of the array (heap overflow); too large and the
trailing slots stay NULL, which is harmless for `execve` but would make
`free_split` stop early — actually not a leak here, since it stops at the first
NULL and everything after it is also NULL. The dangerous direction is
undercounting, and using one shared predicate is what prevents it.

Two passes over the list are accepted rather than one pass with a realloc-style
growth. For a few dozen variables this is free, and it keeps both functions
short enough for Norminette.

### `char **env_to_envp(t_env *env)`

```c
envp = ft_calloc((size_t)env_export_count(env) + 1, sizeof(char *));
if (!envp)
	return (NULL);
```
`+ 1` reserves the slot for the NULL terminator. `execve` has no length
parameter — it walks the array until it hits NULL — so a missing terminator
means the kernel reads whatever follows on the heap and the exec either fails
with garbage in the environment or crashes.

`ft_calloc` rather than `malloc` is what actually writes that terminator: the
memory comes back zeroed, so the last slot is already NULL and no explicit
`envp[i] = NULL` is needed. It also means every slot the loop has not reached
yet is NULL, which is what makes the `free_split` on the error path below
correct.

```c
i = 0;
while (env)
{
	if (env->value)
	{
		envp[i] = make_pair(env);
```
Walks the list in order, so the child's environment appears in the same order
as the parent's, matching bash's `env` output. `i` advances only inside the
`if`, while `env` advances every iteration — the two indices deliberately move
at different rates because skipped nodes must not leave a hole in the array.
That is the reason for the separate counter rather than reusing a single index.

```c
		if (!envp[i])
		{
			free_split(envp);
			return (NULL);
		}
		i++;
	}
	env = env->next;
}
return (envp);
```
The failure path frees the **whole partially built array**, not just the
elements written so far, and this is the memory-ownership point of the file.
`free_split` walks until it finds a NULL element, freeing each string, then
frees the array. Because `ft_calloc` zeroed everything, the first unwritten
slot is NULL and the walk stops exactly at the boundary between "allocated by
this call" and "never touched". So every `make_pair` result produced before the
failure is released, and nothing beyond is touched. Returning NULL without this
call would leak one string per variable already converted.

The caller sees NULL and treats it as a failure; in `exec_child.c` the returned
array is passed to `execve` and, if `execve` returns (meaning it failed), the
array is released with `free_split` there too.

```c
/* Rebuilt on every execve rather than cached */
```
The comment above the function is the design decision worth defending out loud.
The array is a **snapshot**, and `export` or `unset` can change the environment
between two commands, so a cached array would go stale — `export A=1 && env`
would not show `A`, and worse, an `unset` would leave the cached array holding
pointers to strings freed by `env_unset`, which is a use-after-free at the
moment of `execve`. Rebuilding costs one small allocation per external command
and removes an entire class of invalidation bugs. It also means nobody has to
own or free a long-lived copy: the array's lifetime is exactly one `execve`
attempt, inside the child process.

### `int env_key_is_valid(const char *key)`

Called by `builtin_export` on each argument, and by `builtin_unset` (which
additionally requires that the argument contain no `=`). It accepts a **whole**
argument — `KEY`, `KEY=VALUE` or `KEY+=VALUE` — and validates only the part
before the `=`, so callers do not have to split the string first just to check
it.

```c
if (!key || !key[0])
	return (0);
if (!ft_isalpha(key[0]) && key[0] != '_')
	return (0);
```
An empty name is invalid (`export ""` is an error in bash), and the first
character may not be a digit. These are C identifier rules, and bash uses the
same ones: `export 1A=x` prints `not a valid identifier` and returns 1. The
leading-digit rule exists in the shell for a concrete reason — `$1`, `$2` are
positional parameters, so a name starting with a digit would be ambiguous with
them.

Note that this rejects a leading digit specifically by requiring alpha or `_`;
`ft_isalnum` is used from the second character onwards, which is where digits
become legal.

```c
i = 1;
while (key[i] && key[i] != '=')
{
```
Stops at the first `=` — everything from there on is the value and is
unrestricted, so `export A="1 2; #"` is fine. Also stops at end of string,
which is the bare `export A` form.

```c
	if (key[i] == '+' && key[i + 1] == '=')
		return (1);
```
The `+=` escape hatch. A `+` is not a valid identifier character and would be
rejected by the `ft_isalnum` test on the next line, so it has to be recognised
first — but only when it is *immediately* followed by `=`, which is what makes
it the append operator rather than a stray character. `export A+B=1` therefore
falls through to the next line and is correctly rejected, while `export A+=1`
returns valid.

Reading `key[i + 1]` is safe: the loop condition guarantees `key[i]` is not the
NUL, so index `i + 1` is at worst the NUL itself, still inside the buffer.

One consequence worth stating: `export A+` (a `+` at the very end, no `=`)
reaches this test, sees `key[i + 1] == '\0'`, falls through, and is rejected by
the alnum check. Bash rejects it too.

```c
	if (!ft_isalnum(key[i]) && key[i] != '_')
		return (0);
	i++;
}
return (1);
```
Every remaining character must be a letter, digit or underscore. Anything else
— a space, a dot, a hyphen — makes the name invalid, which is how
`export a-b=1` gets its diagnostic.

The final `return (1)` covers both "ran off the end" (bare `export NAME`) and
"hit the `=`" (assignment), since both leave the loop with every inspected
character valid.

### `int env_append(t_env **env, char *key, const char *add)`

Implements `export A+=B`: keep whatever `A` already held and concatenate `B`.
Reached only from `env_set_from_string` in `env_init.c`, which has already
detected the `+` immediately before the `=`.

```c
key[ft_strlen(key) - 1] = '\0';
```
The mutation to be very precise about. On entry, `key` is the text before the
`=`, so for `A+=B` it is the two characters `"A+"`. Writing `'\0'` over the
trailing `+` shortens the string in place to `"A"` — the real variable name.
It does not reallocate and it does not copy; the buffer is one byte longer than
the string it now holds.

This **mutates the caller's buffer**, which is why the parameter is `char *`
and not `const char *` — the signature advertises the mutation. It is safe only
because `env_set_from_string` passes a fresh `ft_substr` allocation that it
frees itself immediately after this call, and because that same caller
guarantees the last character really is a `+` before dispatching here. If
either precondition were broken — say the caller passed a literal or one of
`main`'s `envp` strings — this line would be undefined behaviour or would
corrupt a name. Nothing in the function re-checks that the last character is
`+`; it trusts the caller entirely, and that is the fragile part of the
contract.

There is also no guard against `ft_strlen(key) == 0`, which would index `[-1]`.
Unreachable in practice because `env_set_from_string` only routes here when
`eq != assignment`, meaning at least one character precedes the `=`, and
because `builtin_export` has already run `env_key_is_valid`.

```c
old = env_get(*env, key);
if (!old)
	old = "";
```
Fetch the current value. `env_get` returns NULL both for "does not exist" and
for "declared without a value", and both collapse to the empty string here, so
`export A+=B` on an unset `A` yields `A=B` — identical to plain `A=B`. That is
bash's behaviour, and it is the reason the fallback is `""` rather than an
error.

`old` is a **borrowed** pointer into `node->value`, or a string literal. Either
way it is never freed here. The literal assignment is the reason the variable
cannot be declared `char *` and freed unconditionally; mixing owned and
borrowed pointers in one variable is only safe because nothing ever frees it.

```c
joined = ft_strjoin(old, add);
if (!joined)
	return (-1);
rc = env_set(env, key, joined);
free(joined);
return (rc);
```
Order matters here and it is the use-after-free trap. `ft_strjoin` reads `old`
— which points **inside the node** — and produces a new string. Only then does
`env_set` run, and `env_set` frees the node's old value before installing the
copy of `joined`. Because the read is fully complete before the free, there is
no aliasing problem. Swapping these two statements, or trying to be clever and
pass `old` to `env_set` afterwards, would be a genuine use-after-free.

`env_set` copies `joined`, so `joined` is a temporary and is freed here — one
allocation, one free, on both the success and failure paths since `rc` is
captured first. `-1` on the join failure follows the module-wide convention;
`builtin_export` turns any negative return into exit status 1.

## Things to be ready to explain

- **Why is the `char **` rebuilt for every `execve` instead of cached?**
  Because `export` and `unset` can change the environment between two commands.
  A cached array would go stale, and after an `unset` it would hold pointers to
  strings that `env_unset` already freed — a use-after-free at exec time.
  Rebuilding is one small allocation per external command and eliminates the
  whole invalidation problem.
- **Why do `env_export_count` and `env_to_envp` skip nodes whose `value` is
  NULL?** Those are variables declared with `export X` but never assigned. Bash
  lists them in `export` output but does not pass them to children, so
  `export X; env | grep X` prints nothing. Both functions must use the same
  predicate or the array size and the fill loop would disagree.
- **What does `free_split` free on the error path, and why is it exactly
  right?** It frees every string up to the first NULL element, then the array.
  Because `ft_calloc` zeroed the block, the first unwritten slot is NULL, so
  the walk stops precisely at the boundary between the pairs already built by
  `make_pair` and the untouched slots. Returning NULL without it would leak one
  string per variable already converted.
- **Why `ft_calloc` rather than `malloc` for the array?** The zeroing supplies
  the NULL terminator that `execve` needs, and it is what makes the
  `free_split` cleanup terminate at the right place.
- **Explain the very first line of `env_append`.** It writes `'\0'` over the
  trailing `+`, shortening the caller's buffer in place from `"A+"` to `"A"`.
  It mutates the caller's memory, which is why the parameter is non-const, and
  it is safe only because `env_set_from_string` passes a fresh `ft_substr`
  allocation that it frees itself. There is no internal check that the last
  character actually is a `+`.
- **What does `export A+=B` do when `A` does not exist?** `env_get` returns
  NULL, the code substitutes the empty string, and the result is `A=B` —
  exactly the same as a plain assignment, matching bash.
- **Why must `ft_strjoin` run before `env_set` in `env_append`?** `old` points
  into the node's own value, and `env_set` frees that value before storing the
  new one. Building the joined string first means the read finishes before the
  free; reversing the order is a use-after-free.
- **Why does `env_key_is_valid` check `+` before the alphanumeric test?** `+`
  is not a valid identifier character, so the alnum test would reject it. The
  `+` is only accepted when immediately followed by `=`, which distinguishes
  the append operator `A+=1` from a genuinely invalid name like `A+B=1`.
