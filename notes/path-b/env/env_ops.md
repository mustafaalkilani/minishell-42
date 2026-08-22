# env_ops.c

The five primitive operations on the environment list: look up a node, read a
value, set or overwrite, remove, and destroy the whole list. Everything else in
the shell — `export`, `unset`, `cd` updating `PWD`/`OLDPWD`, the expander
resolving `$VAR` — goes through these functions rather than touching `t_env`
fields directly. The two behaviours to defend here are that `env_set`
overwrites **in place** (so insertion order never changes) and that a NULL
`value` is a meaningful state rather than an error.

## Walkthrough

### `t_env *env_find(t_env *env, const char *key)`

```c
while (env)
{
	if (!ft_strncmp(env->key, key, ft_strlen(key) + 1))
		return (env);
	env = env->next;
}
return (NULL);
```
Linear search for an exact key match. The one detail worth defending is the
`+ 1` on the length.

`ft_strncmp(a, b, n)` compares at most `n` bytes. With `n = ft_strlen(key)` it
would be a **prefix** match: looking up `PATH` would happily return a node
whose key is `PATHX`, because the first four bytes agree. Adding one makes the
comparison include the terminating NUL of `key`, so the candidate key must also
end at exactly that position. That turns the prefix test into an exact-equality
test. This matters immediately in practice — `HOME` versus `HOSTNAME` is a
prefix relationship in real environments, and `PWD` versus `PWD_BACKUP` is the
kind of thing a tester will construct on purpose.

`ft_strlen` is recomputed on every iteration rather than hoisted into a local.
That is a small avoidable cost and it is only that — the result cannot change
mid-loop since `key` is `const`. Hoisting it would have needed another variable
and Norminette caps locals at five, so it is likely a deliberate trade; state it
as a cost, not as a bug.

The loop reassigns the parameter `env` instead of using a cursor variable. Safe
because the parameter is a by-value copy of the caller's pointer — the caller's
head is untouched. Returning the node (not the value) is what lets `env_set`
mutate in place and what lets callers distinguish "absent" from "present with
NULL value", a distinction `env_get` deliberately throws away.

### `char *env_get(t_env *env, const char *key)`

```c
node = env_find(env, key);
if (!node)
	return (NULL);
return (node->value);
```
The read accessor. It returns NULL in two different situations — the variable
does not exist at all, and the variable exists but was declared without a value
(`export X`) — and callers are expected to treat both identically as "empty".
That merge is not laziness, it is bash's expansion rule: `echo "[$NOPE]"` and
`export X; echo "[$X]"` both print `[]`. Collapsing the two cases here means no
caller has to know about the NULL-value representation at all.

The returned pointer is **borrowed, not owned**. It points directly at
`node->value`, which the list still owns. Two consequences the evaluator may
probe:

- The caller must not `free` it. Nothing in the codebase does.
- It is invalidated by any later `env_set` on the same key (which frees the old
  value) or `env_unset`. So a returned pointer must be used or copied before
  the environment is mutated again. `env_append` in `env_export.c` is the one
  place this is subtle: it holds the result of `env_get` in `old`, builds the
  joined string from it, and only *then* calls `env_set` — the free of the old
  value happens strictly after the last read of it. Reversing those two lines
  would be a use-after-free.

### `int env_set(t_env **env, const char *key, const char *value)`

```c
node = env_find(*env, key);
if (node)
{
	if (!value)
		return (0);
```
Existing key, no value supplied. This is `export PATH` on a `PATH` that is
already set, and the correct answer is to do nothing and succeed. Bash's
`export NAME` on an existing variable only marks it exported; it never wipes
the value. If this branch instead assigned NULL, `export PATH` would silently
destroy the user's `PATH` — an easy and spectacular bug, and one testers do
check. Returning 0 (not an error) is right because nothing failed.

```c
	copy = ft_strdup(value);
	if (!copy)
		return (-1);
	free(node->value);
	node->value = copy;
	return (0);
}
```
Overwrite in place, and note the **order**: allocate first, free second. If the
`free` came first and the `strdup` then failed, the node would be left pointing
at freed memory — a dangling pointer inside a live list, which is far worse
than the failure itself. Allocating into a temporary and only committing once
it succeeded means the list is never observably broken: on failure the node
still holds its original, valid value and `-1` propagates to the caller.

The old value is freed exactly here, and this is the only place an existing
value string is released outside `env_unset`/`env_free`. It also means
`ft_strdup(value)` is safe even when `value` aliases `node->value` — the copy
is made before the original is released. That aliasing is not exercised by any
current caller, but the ordering makes it harmless.

Mutating the node rather than deleting and re-appending is the other decision
in this block. It preserves position in the list, so `export PATH=/new` does
not move `PATH` to the end of `env` output. Bash keeps `env` in a stable order
and sorts only for `export` display, so overwriting in place is what makes the
two shells' `env` outputs comparable.

```c
node = env_new(key, value);
if (!node)
	return (-1);
env_add_back(env, node);
return (0);
```
New key: build a node (which copies both strings — see `env_init.c`) and append
it. `env_add_back` takes `t_env **` because the list may be empty, which is the
entire reason `env_set` itself takes `t_env **` and not `t_env *`.

Note the asymmetry with the existing-key branch: a **new** key with a NULL
value does create a node, with `value == NULL`. That is `export X` on a fresh
name, and it is exactly how a declared-but-unassigned variable enters the list.
So the two NULL-value paths differ on purpose — new name: create an empty
declaration; existing name: leave alone.

Return convention for the whole module: `0` success, `-1` allocation failure.
`builtin_export` maps `-1` to exit status 1.

### `int env_unset(t_env **env, const char *key)`

```c
cur = *env;
prev = NULL;
while (cur)
{
	if (!ft_strncmp(cur->key, key, ft_strlen(key) + 1))
	{
```
The same exact-match comparison as `env_find`, duplicated rather than reused.
The reason is that removal needs the **predecessor** node, which `env_find`
does not report, so the walk has to be done again with a trailing `prev`
pointer. (A single-pointer-to-pointer walk would avoid both the duplication and
the `prev` variable, but the explicit form is the more conventional one.)

```c
		if (prev)
			prev->next = cur->next;
		else
			*env = cur->next;
```
Unlink. `prev == NULL` means the match is the head, and removing the head is
the case that must write through the double pointer — the caller's `sh->env`
itself has to change. Getting this branch wrong is the classic linked-list bug:
`unset` the first variable and either the caller keeps a pointer to freed
memory or the whole list is lost.

Splicing before freeing also means the list is consistent at the moment of the
`free`; there is no window where a live node's `next` points at released
memory.

```c
		free(cur->key);
		free(cur->value);
		free(cur);
		return (0);
	}
	prev = cur;
	cur = cur->next;
}
return (0);
```
The node owned both strings, so both are freed with it. `free(cur->value)` is
unconditional and correct on a declared-without-value node because `free(NULL)`
is a no-op — the same reason `env_new`'s cleanup block needs no branching.

The `return (0)` inside the loop stops after the **first** match, which is safe
because keys are unique by construction: `env_set` always looks up before
inserting, so a duplicate key is never created.

Both exits return 0, including the "not found" fall-through. That is
intentional and matches bash: `unset NOSUCHVAR` succeeds silently with status
0. There is deliberately no diagnostic and no distinct return code, so
`builtin_unset` has nothing to report. Be ready to say this is a decision, not
an oversight — the function has no failure mode at all, which is why its `int`
return is effectively vestigial.

### `void env_free(t_env *env)`

```c
while (env)
{
	next = env->next;
	free(env->key);
	free(env->value);
	free(env);
	env = next;
}
```
Iterative teardown. The whole point of the `next` local is that `env->next`
must be read **before** `free(env)`; reading it afterwards is a use-after-free,
and it is the single most common mistake in list-destroy loops. Every node
frees exactly the two strings it owns plus itself, mirroring `env_new`'s
allocations one for one.

Iterative rather than recursive, so a long environment cannot exhaust the
stack, and it is `void` because a free cannot fail.

Called from two places with different meanings: `env_init`'s error path, where
it discards a partially built list, and shutdown, where it releases the real
environment. After it returns the caller's pointer is dangling — the function
takes `t_env *` by value and cannot NULL it out — so callers must not reuse the
variable. `env_init` returns NULL immediately afterwards, which is what keeps
that safe.

## Things to be ready to explain

- **Why `ft_strlen(key) + 1` in the comparison?** To include the terminating
  NUL so the match is exact rather than a prefix match. Without the `+ 1`,
  looking up `HOME` could match a node keyed `HOSTNAME`, and `PWD` could match
  `PWD_BACKUP`.
- **Why does `env_set` allocate the copy before freeing the old value?** So a
  failed `ft_strdup` leaves the node intact. Freeing first would leave a live
  node in the list pointing at released memory if the allocation then failed —
  a dangling pointer is a much worse outcome than the allocation failure
  itself.
- **Why does `env_set` return 0 without doing anything when the key exists and
  `value` is NULL?** That is `export PATH` on an already-set variable, which in
  bash only marks it exported and must not touch the value. Assigning NULL
  there would silently erase the user's `PATH`.
- **Why overwrite in place instead of removing and re-appending?** To keep the
  variable's position in the list, so `env` output stays in the original
  insertion order and matches bash. `export` display sorts separately.
- **Why does `env_unset` need `prev` when `env_find` already locates the
  node?** Unlinking requires the predecessor, which `env_find` does not return,
  so the walk is repeated with a trailing pointer. The `prev == NULL` branch
  handles removal of the head by writing through the `t_env **`.
- **Why does `env_unset` return 0 when the key was not found?** Because
  `unset NOSUCHVAR` succeeds in bash. The function has no failure mode; its
  `int` return exists only for signature consistency with the other operations.
- **Who owns the string returned by `env_get`?** The list does. It is borrowed,
  must not be freed by the caller, and is invalidated by the next `env_set` on
  that key or by `env_unset` — which is why `env_append` builds its joined
  string before calling `env_set`, not after.
- **Why the `next` temporary in `env_free`?** `env->next` has to be read before
  the node is freed. Reading it after `free(env)` is a use-after-free.
