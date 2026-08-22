# builtin_export_list.c

The output half of `export`: everything needed to print `declare -x KEY="VALUE"`
lines in sorted order. It is a separate file from `builtin_export.c` only because
Norminette limits a file to five functions, and this half needs four. The design
decision worth defending is that it sorts an array of *pointers* into the list
rather than reordering the list itself, so `env` keeps its inherited insertion
order while `export` appears alphabetical, matching bash.

## Walkthrough

### `static void print_declare(t_env *node)`

```c
ft_putstr_fd("declare -x ", STDOUT_FILENO);
ft_putstr_fd(node->key, STDOUT_FILENO);
```
Bash's `export` with no arguments prints in `declare -x` form, which is
re-inputtable: you can copy a line back into the shell and get the same variable.
The prefix and the key are always printed.

```c
if (node->value)
{
    ft_putstr_fd("=\"", STDOUT_FILENO);
    ft_putstr_fd(node->value, STDOUT_FILENO);
    ft_putstr_fd("\"", STDOUT_FILENO);
}
ft_putchar_fd('\n', STDOUT_FILENO);
```
The conditional `=` part is the visible consequence of the `value == NULL`
convention. `export X` produces a node with a `NULL` value and prints as
`declare -x X` with no `=`; `export X=` produces a node whose value is the empty
string, so it prints as `declare -x X=""`. That is precisely how bash
distinguishes the two, and it is the same flag that decides whether the variable
is handed to a child (see `env_to_envp`, which skips `NULL` values). Testing
`node->value` here rather than `node->value[0]` is what keeps the two files
consistent — checking the first character instead would wrongly collapse `X` and
`X=` into the same output.

Known simplification: bash escapes characters inside the quotes (a value
containing `"` comes back as `\"`, and `$` and backslash are escaped too). This
prints the raw bytes, so `export A='say "hi"'` produces a line that is not
re-inputtable. Nobody in the mandatory part tests it, but claim only what the
code does.

Writes go to `STDOUT_FILENO`, so `export > file` and `export | grep PATH` work;
the redirection was installed by `run_parent_builtin` before the builtin ran.

### `static int env_size(t_env *env)`

```c
n = 0;
while (env)
{
    n++;
    env = env->next;
}
return (n);
```
A plain length count. It is needed *before* the array can be allocated, because
the array must be sized in one `malloc` — a singly linked list gives no O(1)
length. Note it counts **every** node, including the `value == NULL` ones,
because unlike `env` the `export` listing shows those too. That asymmetry with
`env_export_count` in `path-b/env/env_export.c`, which counts only nodes with a
value, is intentional and is the whole distinction between the two listings.

The parameter is shadowed and advanced locally; the caller's head pointer is
untouched, since `t_env *` is passed by value.

### `static void sort_env_array(t_env **arr, int n)`

```c
i = 0;
while (i < n - 1)
{
    j = 0;
    while (j < n - 1 - i)
    {
```
Textbook bubble sort. Quadratic, but `n` is the number of environment variables —
typically 30 to 60 — so the cost is a few thousand comparisons once per `export`
invocation, which is invisible. It is chosen over anything smarter because it
fits Norminette's line and variable limits and needs no recursion or helper.
The `- i` in the inner bound is the standard optimisation: after pass `i` the
last `i` elements are already in place.

Both bounds are correct for `n == 1` (outer condition `0 < 0` is false, so the
function does nothing) and cannot underflow, because the caller guarantees
`n >= 1`.

```c
        if (ft_strncmp(arr[j]->key, arr[j + 1]->key,
                ft_strlen(arr[j]->key) + 1) > 0)
        {
            tmp = arr[j];
            arr[j] = arr[j + 1];
            arr[j + 1] = tmp;
        }
```
The comparison length is `strlen(left) + 1`, which makes `ft_strncmp` behave as a
full `strcmp`: the `+1` includes the left key's terminating `'\0'` in the
comparison window, so a key that is a strict prefix of the other still compares
correctly. Trace `"A"` against `"AB"` with n = 2: the loop stops when `s1[1]` is
`'\0'`, and the return is `0 - 'B'`, negative, so `"A"` sorts first. The mirror
case `"AB"` against `"A"` uses n = 3 and returns positive. Both are what a plain
`strcmp` would say.

Only the pointers in `arr` move; `->next` is never touched, so the underlying
list is not reordered. That is the reason `env` still prints in inherited order
after an `export` has been run.

The ordering is bytewise, not locale-aware — uppercase before lowercase, `_`
(0x5F) after uppercase letters and before lowercase. Bash's `export` is also
bytewise: its `sort_variables` calls `qsort` with a comparator that uses plain
`strcmp`, so the two orderings agree.

### `int export_list(t_env *env)`

```c
n = env_size(env);
if (n == 0)
    return (EXIT_OK);
```
The empty-environment guard. It exists so that `malloc(0)` is never called —
which is implementation-defined (it may return `NULL`, which would be
indistinguishable here from a real failure and would wrongly return 1). Reachable
in practice with `env -i ./minishell` followed by `export`.

```c
arr = malloc(sizeof(t_env *) * (size_t)n);
if (!arr)
    return (1);
```
One allocation for the whole function: an array of `n` **borrowed** pointers. It
holds no copies, so nothing inside it is ever freed — freeing an element would
destroy a live environment node. The cast to `size_t` silences the signed/
unsigned multiplication; `n` came from a list walk so it cannot be negative.

On allocation failure the function returns **1** and prints nothing. Bash has no
such status because it cannot fail this way, so 1 ("general error") is the
sensible stand-in. This value propagates straight out of `builtin_export`, so
`export` alone would report `$?` as 1.

```c
i = 0;
while (env)
{
    arr[i] = env;
    env = env->next;
    i++;
}
```
Fills the array in list order. The loop is bounded implicitly by the list rather
than by `n`, which is safe only because `n` was computed from the same list and
nothing can modify it in between — this is single-threaded and no signal handler
touches the environment. Worth saying out loud if asked why there is no `i < n`
guard.

```c
sort_env_array(arr, n);
i = 0;
while (i < n)
    print_declare(arr[i++]);
free(arr);
return (EXIT_OK);
```
Sort, print, release. `free(arr)` releases only the pointer array; every `t_env`
node it pointed at is still owned by `sh->env` and is still alive. Getting that
backwards — freeing the elements — would be the memory bug an evaluator hunts for
in this file.

`EXIT_OK` is 0, which is what bash returns for a successful `export` listing.

## Things to be ready to explain

- **Why sort a copy of the pointers instead of the list?** Because `env` must
  keep the inherited insertion order while `export` must look alphabetical, and
  both read the same `t_env` list. Reordering `->next` would corrupt `env`'s
  output. The array holds borrowed pointers, so sorting it is free of any
  ownership question.
- **What exactly does `free(arr)` free?** Only the array of pointers. The nodes
  remain owned by `sh->env` and are freed by `env_free` at shutdown. Freeing the
  elements here would leave the environment full of dangling pointers.
- **Why does `declare -x X` sometimes have no `="..."`?** Because that node's
  `value` is `NULL`, the representation of `export X` — declared but never
  assigned. `export X=` gives an empty non-NULL string and prints
  `declare -x X=""`. The same `value != NULL` test decides whether the variable
  is passed to child processes.
- **Why `ft_strncmp(a, b, ft_strlen(a) + 1)` rather than a fixed length?** The
  `+1` pulls the terminating NUL into the comparison, turning `strncmp` into a
  full `strcmp`, so prefixes order correctly (`A` before `AB`). A shorter length
  would report equality for a key that merely starts the same way.
- **Is bubble sort acceptable here?** Yes: `n` is the number of environment
  variables, a few dozen, and the sort runs once per `export` with no arguments.
  It was chosen because it fits in one Norminette-legal function with no helper
  and no recursion. Bash itself uses `qsort` with a `strcmp` comparator, so the
  resulting order is identical.
- **When can `export_list` return non-zero?** Only 1, and only if the pointer
  array could not be allocated. The empty-environment case returns 0 early
  without allocating, which also avoids the implementation-defined `malloc(0)`.
