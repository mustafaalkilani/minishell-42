# env_init.c

Builds the shell's environment out of the `char **envp` the kernel handed to
`main`, and owns the two constructors every other env file leans on: `env_new`
(allocate one `t_env` node) and `env_add_back` (append it). It also owns the
parser that turns a flat `"KEY=VALUE"` string back into a key/value pair, which
is the same code path used by the `export` builtin. Finally it applies the one
startup mutation bash performs on its own environment: incrementing `SHLVL`.

The type it works on is declared in `includes/minishell.h`:

```c
typedef struct s_env
{
	char			*key;
	char			*value;
	struct s_env	*next;
}	t_env;
```

Three invariants hold for every node in the list and are worth memorising,
because most of the reasoning below is just these three restated:

1. `key` is **always** a non-NULL, heap-allocated string owned by the node.
2. `value` is either a heap-allocated string owned by the node, **or `NULL`**.
   `NULL` is not an error state — it is the representation of a variable that
   was declared but never assigned (`export X`). Bash lists such a variable in
   `export` output but does **not** put it in the environment of a child
   process, and that single rule is why `env_export_count` and `env_to_envp`
   in `env_export.c` skip these nodes.
3. Order is insertion order, and it is never re-sorted. `env` prints in this
   order; `export` sorts a temporary view instead of the list itself.

## Walkthrough

### `t_env *env_new(const char *key, const char *value)`

```c
node = malloc(sizeof(t_env));
if (!node)
	return (NULL);
```
One node, uninitialised. Nothing else can be allocated until this succeeds,
because the two string pointers live inside it — if `malloc` fails there is
nothing to clean up and `NULL` propagates straight out. Every caller
(`env_set`) checks for `NULL` and turns it into `-1`.

```c
node->key = ft_strdup(key);
node->value = NULL;
if (value)
	node->value = ft_strdup(value);
```
Both strings are **copied**. This is the ownership decision of the whole
module: the node never aliases the caller's memory. It matters concretely
because callers pass in pointers with much shorter lifetimes — `env_set_from_string`
passes `eq + 1`, a pointer *into* the middle of the caller's `assignment`
buffer, and `env_bump_shlvl` passes a `new` string it frees three lines later.
If the node stored the pointer instead of a copy, both would be dangling
immediately.

`node->value = NULL` is set **before** the `if`, not inside an `else`. That is
what makes the NULL-value case free: when `value` is NULL the field is already
correct, and — more importantly — the error branch below can call
`free(node->value)` unconditionally without reading an uninitialised pointer.
Order here is load-bearing, not stylistic.

```c
if (!node->key || (value && !node->value))
{
	free(node->key);
	free(node->value);
	free(node);
	return (NULL);
}
```
The partial-allocation cleanup, and the part evaluators most often poke at.
Read the condition carefully:

- `!node->key` — the key strdup failed. Always a real failure; a node with a
  NULL key would break invariant 1 and would crash `env_find`, which calls
  `ft_strncmp(env->key, ...)` with no NULL guard.
- `(value && !node->value)` — the value strdup failed. The `value &&` guard is
  essential: without it, every legitimate `export X` (value deliberately NULL)
  would be misread as an allocation failure and the node would be thrown away.
  So the test is not "is value NULL" but "did I *ask* for a value and not get
  one".

The cleanup itself frees whichever of the two strings actually got allocated,
then the node. `free(NULL)` is defined to do nothing, so no branching is
needed: if `key` succeeded and `value` failed, `free(node->key)` releases the
real allocation and `free(node->value)` is a no-op on NULL. Skipping this block
and just returning NULL would leak the key string on every value-allocation
failure. Nothing is half-inserted at this point — the node has not been linked
into any list yet — so freeing it is safe and complete.

```c
node->next = NULL;
return (node);
```
`next` is set only on the success path, which is fine because the failure path
never returns the node to anyone. It must be NULL rather than garbage: `env_add_back`
appends by walking until `cur->next` is NULL, and `env_free` stops on NULL.

### `void env_add_back(t_env **head, t_env *node)`

```c
if (!*head)
{
	*head = node;
	return ;
}
```
The double pointer exists purely for this case. Appending to a non-empty list
only mutates a node the caller already reaches, but appending to an *empty*
list has to change the caller's own `head` variable, which is impossible
through a single pointer. The same reason drives the `t_env **env` parameter on
`env_set`, `env_unset` and `env_set_from_string`.

```c
cur = *head;
while (cur->next)
	cur = cur->next;
cur->next = node;
```
Linear walk to the tail, then link. This is O(n) per insertion, so building the
environment at startup is O(n^2) in the number of variables. That is knowingly
accepted: a real environment is a few dozen entries, and the alternative —
keeping a tail pointer — would mean carrying more state around for no
observable gain. Appending rather than prepending is what preserves invariant 3,
and therefore what makes `env` print variables in the order the parent process
supplied them, exactly like bash.

No allocation happens here and nothing is freed; the function takes ownership
of `node` and the list becomes responsible for it from this point on.

### `int env_set_from_string(t_env **env, const char *assignment)`

The single entry point for "here is a raw `KEY`, `KEY=VALUE` or `KEY+=VALUE`
string, put it in the environment". Used by `env_init` for each inherited
`envp` entry, and by `builtin_export` for each argument.

```c
eq = ft_strchr(assignment, '=');
if (!eq)
	return (env_set(env, assignment, NULL));
```
Split at the **first** `=`, which is the POSIX rule: everything before it is the
name, everything after is the value, and any further `=` characters are part of
the value. `export A=b=c` therefore sets `A` to `b=c`.

No `=` at all means `export X` — declared, not assigned — so `env_set` is
called with a NULL value, which produces exactly the NULL-value node described
in invariant 2. Note that `env_set` treats "no value" specially for an existing
key: `export PATH` on an already-set `PATH` leaves the value untouched, which is
bash's behaviour.

```c
key = ft_substr(assignment, 0, (size_t)(eq - assignment));
if (!key)
	return (-1);
```
The key has to be copied out because the source is one contiguous string and we
need a NUL-terminated name. Pointer arithmetic `eq - assignment` is the length
of the part before `=`. `-1` is the module-wide "allocation failed" code;
`builtin_export` turns it into exit status 1.

Note the degenerate input `"=VALUE"`: `eq == assignment`, the substr has length
0, and `env_set` would create a node with an empty key. This is unreachable
from `export` because `builtin_export` calls `env_key_is_valid` first, which
rejects an empty first character. It is theoretically reachable from `env_init`
if the parent process supplied such an entry, which no sane parent does.

```c
if (eq != assignment && eq[-1] == '+')
	rc = env_append(env, key, eq + 1);
else
	rc = env_set(env, key, eq + 1);
```
This is the `+=` detection, and it is done by looking **backwards** one
character from the `=` rather than forwards for a `+`. The `eq != assignment`
guard is what keeps `eq[-1]` from reading the byte before the buffer — for the
`"=VALUE"` input above, `eq` points at index 0 and `eq[-1]` would be an
out-of-bounds read. Cheap guard, real undefined behaviour prevented.

`key` at this point still contains the trailing `+` (it was cut at the `=`, so
`A+=b` gives `key = "A+"`). `env_append` is documented to strip it, and it does
so **by writing into this buffer**, which is precisely why `key` here is a
freshly allocated mutable copy rather than a pointer into `assignment` — the
`assignment` argument is `const char *` and may well be one of `main`'s `envp`
strings, which must not be modified.

`eq + 1` is the value: a pointer into `assignment`, valid only for the duration
of the call. Safe because `env_set`/`env_append` copy it.

```c
free(key);
return (rc);
```
The single free for the single allocation, on both the success and failure
paths, because `rc` is captured first. This function allocates one thing and
frees one thing; it never leaves ownership of `key` with anyone else.

### `static void env_bump_shlvl(t_env **env)`

```c
old = env_get(*env, "SHLVL");
level = 1;
if (old)
	level = ft_atoi(old) + 1;
```
Every shell records its nesting depth, and a shell launched from another shell
must publish one more than it inherited. If `SHLVL` was absent — the shell was
started from something that is not a shell — the default is 1, the level of a
top-level shell.

`env_get` returns NULL both for "not present" and for "present but declared
without a value", so `SHLVL` declared-but-unset also lands on level 1. That
merging is intentional throughout this module and matches how bash treats an
unset variable during expansion.

`ft_atoi` on garbage (`SHLVL=abc`) returns 0, so `level` becomes 1. Bash prints
a warning and resets to 1 in that case; here the reset happens silently. The
observable value ends up the same, only the diagnostic is missing.

```c
if (level < 1)
	level = 1;
```
Clamp. Guards the case where the inherited value was negative or where `ft_atoi`
produced something that would make `level` zero or below — `SHLVL=-5` would
otherwise publish `-4`. Bash also refuses to go below 1. Note it does **not**
guard the upper end: bash resets to 1 above 999, and this implementation keeps
counting. A minor divergence, only visible after nesting a thousand shells.

```c
new = ft_itoa(level);
if (!new)
	return ;
env_set(env, "SHLVL", new);
free(new);
```
`ft_itoa` allocates; `env_set` copies; `new` is freed here. The function is
`void`, so an allocation failure is swallowed with an early `return` — the
shell keeps the inherited `SHLVL` rather than aborting startup. That is a
deliberate "degrade, do not die" choice: a wrong `SHLVL` is cosmetic, a failed
startup is not. The `env_set` return value is likewise ignored for the same
reason.

Being `static` matters: this is startup-only policy, not a general operation,
and nothing outside this file should be able to bump `SHLVL` a second time.

### `t_env *env_init(char **envp)`

```c
head = NULL;
i = 0;
while (envp && envp[i])
{
```
`head = NULL` is the empty list, and it is a valid state — `env -i ./minishell`
starts the shell with a completely empty environment and everything downstream
must cope. The `envp &&` guard covers the (rare but legal) case of `envp`
itself being NULL.

```c
	if (env_set_from_string(&head, envp[i]) < 0)
	{
		env_free(head);
		return (NULL);
	}
	i++;
}
```
Each inherited string is parsed through the same path `export` uses, so there
is exactly one implementation of "KEY=VALUE" semantics in the codebase.

The failure path frees **everything built so far** and returns NULL. This is
the all-or-nothing contract: `env_init` either hands back a complete list that
the caller owns, or it hands back NULL having freed every byte it allocated.
There is no partial list for the caller to worry about, which is why `main` can
treat a NULL return as a plain fatal-error exit without any cleanup of its own.

Note that no validation is performed on inherited entries — `env_key_is_valid`
is not called here. That is correct: bash imports whatever the parent gave it,
including names it would refuse from `export`, and re-exports them.

```c
env_bump_shlvl(&head);
return (head);
```
The bump happens **once**, after the import, and only here. Doing it inside the
loop would increment per-variable; doing it before the loop would be overwritten
by the inherited `SHLVL` entry. Ordering is the whole point of the line's
position. Without this call, `env` output differs from bash on the `SHLVL` line
in every comparison test.

The returned list is owned by the caller (`main`, which stores it in
`sh->env`), and is released by `env_free` at shutdown.

## Things to be ready to explain

- **Why does `env_new` set `node->value = NULL` before the `if (value)` instead
  of using an `else`?** So the error block can call `free(node->value)`
  unconditionally without ever reading an uninitialised pointer, and so the
  declared-without-value case needs no extra branch. It is what makes the
  cleanup three unconditional `free` calls instead of a nest of `if`s.
- **In the error check, why `(value && !node->value)` and not just
  `!node->value`?** Because a NULL `value` is a legitimate state, not a
  failure. Without the guard, every `export X` would be treated as an
  out-of-memory condition and the node would be discarded. The test asks "did I
  request a copy and fail to get one", not "is the field NULL".
- **What exactly is freed if the second `ft_strdup` fails?** The key string and
  the node itself; `free(node->value)` is a no-op on the NULL that the failed
  strdup returned. Nothing has been linked into a list yet, so there is no
  dangling pointer anywhere and the caller sees a plain NULL.
- **Why is `+=` detected with `eq[-1] == '+'` and why the `eq != assignment`
  guard?** The key was already cut at the first `=`, so the `+` is the last
  character before it — looking back one byte is the direct test. The guard
  prevents reading one byte before the start of the buffer when the string
  begins with `=`.
- **Why must `env_set_from_string` pass a heap copy of the key to
  `env_append`?** `env_append` strips the `+` by writing a `'\0'` into that
  buffer. The original `assignment` is `const char *` and is frequently one of
  the process's real `envp` strings, which must not be mutated.
- **Why is `SHLVL` bumped after the import loop rather than during it?** The
  inherited `SHLVL=n` entry is imported by the loop; incrementing before it
  would simply be overwritten, and incrementing inside the loop would happen
  once per variable. Once, afterwards, is the only correct position.
- **What happens if `env_set_from_string` fails halfway through `env_init`?**
  The partial list is freed with `env_free` and NULL is returned, so the caller
  never receives a half-built environment. `env_init` is all-or-nothing.
- **Why append instead of prepend?** To preserve insertion order, which is what
  `env` prints. Prepending would reverse the inherited environment and every
  `env` diff against bash would fail.
