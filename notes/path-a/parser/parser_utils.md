# parser_utils.c

The allocation and cleanup helpers behind the parser: creating an empty `t_cmd`,
growing a command's `argv`, and destroying a redirection list. Nothing here makes
syntactic decisions; `parser.c` and `parser_redir.c` do that and call in here to
build the structures. One function in this file, `free_redirs`, does more than
free memory — it is also where heredoc file descriptors get closed.

## Walkthrough

### `t_cmd *cmd_new(void)`

```c
cmd = malloc(sizeof(t_cmd));
if (!cmd)
    return (NULL);
cmd->argv = ft_calloc(1, sizeof(char *));
if (!cmd->argv)
{
    free(cmd);
    return (NULL);
}
cmd->redirs = NULL;
cmd->next = NULL;
```

The important line is `ft_calloc(1, sizeof(char *))`. A brand-new command gets a
one-element array containing a single NULL, i.e. a valid, already
NULL-terminated `argv` with zero arguments — not a NULL pointer.

That invariant pays off in three places. `parse_pipe` can test `(*cur)->argv[0]`
without a NULL check to decide whether the left-hand side of a pipe is empty.
`cmd_add_arg` can count the existing arguments with `while (cmd->argv[n])`
without a special first-insert case. And path-b can hand `argv` to `execve` or
inspect `argv[0]` without ever testing the array pointer itself.

The failure path frees `cmd` before returning, so a partially constructed command
is never leaked and never escapes.

### `int cmd_add_arg(t_cmd *cmd, char *value)`

```c
n = 0;
while (cmd->argv[n])
    n++;
grown = ft_calloc(n + 2, sizeof(char *));
if (!grown)
{
    free(value);
    return (-1);
}
```

Counts the existing arguments, then allocates a whole new array. The `n + 2` is
the classic off-by-one spot and is worth being able to justify out loud: `n`
existing arguments, plus 1 for the new one, plus 1 for the NULL terminator.
Using `ft_calloc` rather than `malloc` means the terminator slot is already zero
and never has to be written explicitly.

The failure branch calls `free(value)`. That is the ownership contract: this
function takes ownership of `value` the moment it is called, so the caller in
`parse_step` never has to clean up after a failed insert.

```c
ft_memcpy(grown, cmd->argv, n * sizeof(char *));
grown[n] = value;
free(cmd->argv);
cmd->argv = grown;
return (0);
```

The `memcpy` is a shallow copy of `n` **pointers**, not of the strings. Ownership
of the strings moves to the new array, which is why only `cmd->argv` (the array
of pointers) is freed and not the strings it contained — freeing those would
leave the new array full of dangling pointers.

Growing by full reallocation on every argument makes building an `argv` of `n`
elements O(n²) in copies. That is a deliberate trade: a command line has a
handful of arguments, and the alternative is carrying a capacity field in
`t_cmd` and keeping it in sync everywhere.

One thing to be aware of: `value` is never tested for NULL. If a caller passed
the result of a failed `ft_strdup`, `grown[n]` would be set to NULL and the
function would return 0 for success, silently truncating `argv` at that point
instead of reporting the failure. That is why the contract is enforced on the
other side: `parse_step` in `parser.c` checks its `ft_strdup` result before
calling in here and returns -1 itself if it is NULL. Only a non-NULL string ever
reaches this function.

### `void free_redirs(t_redir *redirs)`

```c
while (redirs)
{
    next = redirs->next;
    free(redirs->filename);
    if (redirs->heredoc_fd >= 0)
        close(redirs->heredoc_fd);
    free(redirs);
    redirs = next;
}
```

Destroys a redirection list. `next` is saved before the node is freed, as always.

The `close` is the part that makes this more than a memory helper. Heredoc bodies
are read before the pipeline forks and are handed to the command as an open file
descriptor stored in `heredoc_fd`. If the command list were freed without closing
them — for instance when the shell hits a later error, or simply at the end of a
successful line — the shell process would leak a descriptor per heredoc and
eventually hit `EMFILE`.

The `>= 0` guard is what makes it safe to free a command list at any point in its
lifetime. `redir_new` initialises `heredoc_fd` to -1 and only path-b ever sets a
real descriptor, so a redirection that never had a heredoc, or a heredoc that was
never read because parsing failed first, is simply skipped. Calling `close(-1)`
would not be catastrophic (it just returns `EBADF`) but closing an
uninitialised value could close an unrelated descriptor such as stdin.

This function is called only from `free_cmds` in `parser.c`, which is itself
reachable both from `parse`'s error path and from `main` after execution.

## Things to be ready to explain

- **Why does a new command get a one-element `argv` instead of NULL?**
  So that `argv` is always a valid NULL-terminated array. `parse_pipe` can read
  `argv[0]`, `cmd_add_arg` can count with a simple loop, and the executor can
  pass it to `execve` without any special-casing.

- **Why `n + 2` in the calloc?**
  `n` existing arguments, one new argument, one NULL terminator. `ft_calloc`
  supplies the terminator for free.

- **Doesn't reallocating on every argument make this quadratic?**
  Yes, in pointer copies. It is irrelevant at command-line scale and it removes
  the need to track a capacity field inside `t_cmd`.

- **After `ft_memcpy`, why free `cmd->argv` but not the strings inside it?**
  The copy is shallow: the new array holds the same string pointers. Only the
  old array of pointers is redundant. Freeing the strings would leave dangling
  pointers in `grown`.

- **Why does `free_redirs` close a file descriptor?**
  Because heredoc bodies are read into a descriptor stored in the redirection
  before the fork. Freeing the command list is the single point where that
  descriptor's lifetime ends, so closing here prevents an fd leak per heredoc.
  The `-1` sentinel set in `redir_new` makes the check safe at any stage.

- **Whatever happened to `cmd_add_back`?**
  This file used to define a `cmd_add_back(t_cmd **head, t_cmd *cmd)` mirroring
  `token_add_back` in `lexer_utils.c`. Grepping for call sites confirmed it was
  never called: `parse_pipe` links the new segment directly with
  `(*cur)->next = next` because it already holds the tail in `cur`, so the
  append is O(1) instead of an O(n) walk from the head. It was dead code kept
  only for symmetry with the token list, and it has since been deleted from both
  this file and `path_a.h`.
