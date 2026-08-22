# path_b.h

The private header for the back end: environment, builtins, executor,
redirections and signals. It declares one type, `t_pipeline`, which exists only
inside the fork-and-pipe loop, and the internal prototypes of the five
subsystems. The public surface of this half is small on purpose — `exec_line`,
the six `env_*` functions, `is_builtin`/`run_builtin` and the four
`signals_setup_*` functions are declared in `minishell.h`, and everything else
stays here. That narrow interface is what allowed both partners to work in
parallel against a stable contract.

## Walkthrough

### Include guard and `# include "minishell.h"`

```c
#ifndef PATH_B_H
# define PATH_B_H

# include "minishell.h"
```
Same structure as `path_a.h`. Every path-b `.c` file includes only
`"../path_b.h"` and gets the shared types, the exit-status constants, libft and
all the system headers transitively. Since both private headers pull in the
same shared header, `t_cmd` and `t_env` are guaranteed to have identical
layouts on both sides of the project — there is exactly one definition of each.

### `typedef struct s_pipeline`

```c
typedef struct s_pipeline
{
    int     prev_read;
    int     pipefd[2];
    pid_t   last_pid;
}   t_pipeline;
```
The three pieces of state that have to survive from one pipeline iteration to
the next. Bundling them into a struct is not cosmetic: `run_pipeline` passes a
`t_pipeline *` to both `pipeline_child` and `pipeline_parent`, and without the
struct each of those helpers would need four or five parameters, which
norminette forbids.

**`prev_read`** is the read end of the pipe created by the *previous*
iteration — the fd this command should use as its stdin. It is `-1` for the
first command in the pipeline, meaning "inherit the shell's stdin unchanged".
Using `-1` as the sentinel rather than `0` matters because `0` is a valid fd
(stdin itself), so a zero sentinel would make `close(pl->prev_read)` shut the
shell's own standard input. Both `pipeline_child` and `pipeline_parent` test
`prev_read != -1` before touching it, and `pipeline_parent` resets it to `-1`
after the last command so nothing dangles.

**`pipefd[2]`** is the pipe connecting *this* command to the next one, created
at the top of each iteration but only when `cmds->next` exists — the last
command in a pipeline needs no outgoing pipe. `pipefd[0]` is the read end,
`pipefd[1]` the write end, which is the order `pipe()` fills them in.

The fd bookkeeping around this field is the part evaluators probe. Every
process must close every descriptor it does not need, because a pipe only
delivers EOF to its reader once **all** copies of the write end are closed.
Miss one and `cat` in the middle of a pipeline hangs forever waiting for input
that will never end. So: the child closes `pipefd[0]` (it is the writer, it has
no business reading), dups `pipefd[1]` onto stdout and then closes the original;
the parent closes `pipefd[1]` immediately (it never writes) and keeps
`pipefd[0]` only long enough to hand it forward as the next iteration's
`prev_read`. Every fd is closed in exactly one place.

**`last_pid`** is the pid of the final command in the pipeline. It is stored
rather than derived because bash's rule is that a pipeline's exit status is the
status of its **last** stage, not the first failure — `false | true` gives 0 and
`true | false` gives 1, both of which are cases in `tests/edges.sh`.
`wait_for_all` reaps every child with `waitpid(-1, ...)` so no zombies are left
even when an early stage exits first, but it only records the status when the
reaped pid matches `last_pid`. It is initialised to `-1` so that a `fork`
failure on the first command cannot match a real pid.

### Prototype group: env internals

```c
t_env   *env_new(const char *key, const char *value);
void    env_add_back(t_env **head, t_env *node);
t_env   *env_find(t_env *env, const char *key);
int     env_key_is_valid(const char *key);
int     env_set_from_string(t_env **env, const char *assignment);
int     env_append(t_env **env, char *key, const char *add);
```
Responsible for the internals of the `t_env` list, behind the six public
`env_*` functions in `minishell.h`.

`env_new` and `env_add_back` are the list primitives; appending to the **tail**
rather than pushing to the head is deliberate, because `env` must print
variables in insertion order (matching the order the kernel handed them to us)
while `export` prints them sorted. Preserving insertion order in the list means
only `export_list` has to sort.

`env_find` returns the node rather than the value, which is what lets `env_set`
overwrite in place instead of unlinking and re-appending — again to preserve
ordering. `env_get` is the public thin wrapper that returns `node->value`, and
it returns NULL both for "not present" and for "declared without a value",
because expansion treats both as empty.

`env_key_is_valid` enforces the identifier rules — first character a letter or
underscore, the rest alphanumeric or underscore — and is what makes
`export 1BAD=x` fail with a diagnostic instead of creating a variable no child
could ever read. It validates only the key portion of a `KEY`, `KEY=VALUE` or
`KEY+=VALUE` string, stopping at the `=` and returning success early when it
sees `+=`.

`env_set_from_string` parses one `KEY=VALUE` argument and is shared between
`env_init` (which feeds it every entry of `envp`) and `builtin_export`. Having
one parser means the startup environment and a user-typed `export` cannot
disagree about where the key ends.

`env_append` implements `export A+=B`. Note it takes a **non-const** `char *key`
because it truncates the trailing `+` in place before looking the key up — a
small, documented mutation of the caller's buffer that avoids an extra
allocation. An absent variable seeds from `""`, so `export NEWV+=abc` creates
`NEWV=abc`, which is the behaviour `tests/edges.sh` checks.

### Prototype group: builtin implementations

```c
int     builtin_echo(char **argv);
int     builtin_cd(char **argv, t_shell *sh);
int     builtin_pwd(void);
int     builtin_env(char **argv, t_shell *sh);
int     builtin_export(char **argv, t_shell *sh);
int     builtin_unset(char **argv, t_shell *sh);
int     builtin_exit(char **argv, t_shell *sh);
int     export_list(t_env *env);
```
Responsible for the seven builtins the subject requires. They are private here
because the only way into them from outside is `run_builtin`, the dispatcher
declared in `minishell.h`.

The signatures are not uniform, and that is intentional. `builtin_echo` takes
only `argv` because it reads and writes nothing in the shell state;
`builtin_pwd` takes nothing at all because it calls `getcwd`. Giving them a
`t_shell *` they would ignore would be noise, and norminette flags unused
parameters. The four that mutate or read shell state take `sh`.

Every one returns an `int` exit status rather than printing it or setting
`sh->last_status` itself, so `run_builtin` has a single uniform return path and
the same function works whether it was called in the parent (a lone builtin) or
in a forked child (a builtin inside a pipeline).

`export_list` is split out from `builtin_export` because `export` with no
arguments has completely different behaviour from `export` with arguments: it
prints the sorted `declare -x` listing rather than setting anything. Keeping
them separate keeps both inside norminette's 25-line limit.

`builtin_exit` is the odd one out: it can call `exit()` and never return. That
is why it needs `sh` — it has to free the environment before leaving, since
nothing above it will get the chance.

### Prototype group: executor internals

```c
char    *resolve_command(char *cmd, t_shell *sh);
void    free_split(char **split);
void    child_exec(t_cmd *cmd, t_shell *sh);
int     wait_for_all(pid_t last_pid);
int     status_to_exit(int status);
```
Responsible for turning a `t_cmd` list into running processes. All private
behind the single public `exec_line`.

`resolve_command` is PATH lookup. Three rules are encoded in it and each is
worth knowing: a name containing `/` is used verbatim and never searched, so
`./cmd` and `/bin/cmd` bypass lookup entirely; an unset or empty `PATH` falls
back to `"."`, which is why `unset PATH; cd /bin; ls` still works; and its
`is_executable_file` helper calls `stat` and rejects directories before calling
`access(X_OK)`, because directories carry the execute bit as "searchable" and
`access` alone would happily accept `/tmp` as a command, producing the wrong
exit code.

`free_split` releases any NULL-terminated `char **`. It is shared between the
`ft_split` of `PATH` and the `env_to_envp` array rather than duplicated, and it
tolerates NULL so error paths need no guard.

`child_exec` runs inside the forked child and **never returns** — every path
ends in `execve` or `exit`. That is a hard requirement, not a style choice: if
it returned, the child would fall back into `run_pipeline`'s loop and start
forking grandchildren of its own. Its order of operations is also fixed:
`signals_setup_child` resets SIGINT and SIGQUIT to `SIG_DFL` so the child dies
normally from ctrl-C; then redirections; then, if the command is a builtin, it
runs it right there and exits, which is how `echo x | cd /tmp | cat` works
while correctly discarding the `cd`.

`status_to_exit` converts a raw `waitpid` status into a shell exit code:
`WIFSIGNALED` gives `128 + WTERMSIG` (130 for ctrl-C, 131 for ctrl-backslash),
`WIFEXITED` gives `WEXITSTATUS`. Isolating this in one function means the
128-offset convention is written down exactly once.

`wait_for_all` reaps until `waitpid` runs out of children, but records the
status only from `last_pid`. Reaping everything prevents zombies; recording
only the last implements bash's pipeline-status rule.

### Prototype group: redirection internals

```c
int     apply_redirs(t_cmd *cmd);
int     collect_heredocs(t_cmd *cmds, t_shell *sh);
void    close_other_heredocs(t_cmd *cmds, t_cmd *self);
```
Three functions, and the split between the first two encodes the most important
timing decision in the executor. The third exists to pay for that decision.

`collect_heredocs` runs **before any fork**, at the very top of `exec_line`,
and walks the whole command list draining every heredoc body into a pipe and
storing the read end in `redir->heredoc_fd`. It has to happen first because
reading a heredoc means reading from the shell's own stdin, which the user is
typing on — a forked child cannot do that on the parent's behalf, and in a
multi-stage pipeline the children's stdin has already been rewired to pipes. It
also has to be able to abort: ctrl-C during a heredoc sets `g_signal` through
the dedicated heredoc handler, `collect_heredocs` returns `-1`, and `exec_line`
returns `128 + SIGINT` without running anything. `tests/signals.py` covers both
that and ctrl-D ending a heredoc without hanging.

`apply_redirs` runs later, in the child (or, for a lone builtin, in the parent
between a `dup`/`dup2` save and restore). It walks the list strictly left to
right so `> a > b` creates both files but leaves stdout on `b`, and its heredoc
case has no file to open at all — the body is already in a pipe, so it just
`dup2`s the stored fd onto stdin and resets the field to `-1` so a second pass
cannot close it twice.

`close_other_heredocs` is the cost of collecting before the fork. Because every
heredoc on the line is drained up front, each child of `run_pipeline` inherits
the read ends belonging to *all* the stages, not just its own — and
`apply_redirs` only ever consumes its own. `pipeline_child` calls this
immediately before `child_exec` to close the rest, so they do not survive
`execve` into the exec'd program. It is declared next to `collect_heredocs`
deliberately: the two are the paired halves of heredoc fd ownership in the
child, and reading either one without the other gives a misleading picture.

The `t_cmd *self` parameter is the command to spare, compared by pointer
identity against the nodes of the list. That works because the child is handed
the exact node it is executing, and it is the only comparison that survives
`cat << A | cat << B`, where the two stages are textually identical.

Note there is no `apply_one`, `open_target` or `close_heredoc_list` prototype
here: those are `static` inside `redir_apply.c`. Only what genuinely crosses a
file boundary gets a declaration in this header. With `close_other_heredocs`
added, `redir_apply.c` holds exactly five functions, which is norminette's
per-file maximum — the next redirection helper, static or not, forces a new
`.c` file.

## Things to be ready to explain

- **Why does `t_pipeline` exist instead of loose variables?** Because
  `prev_read`, `pipefd` and `last_pid` all have to survive from one iteration
  to the next and be visible to both the child and parent helpers. Passing one
  pointer keeps every function within norminette's four-parameter limit.
- **Why is `prev_read` initialised to `-1` and not `0`?** `0` is stdin. A zero
  sentinel would make the "do I need to close the previous read end?" check
  true on the first iteration and close the shell's own standard input.
- **Why must every process close the pipe ends it does not use?** A reader only
  sees EOF once every copy of the write end is closed. One forgotten
  descriptor and a middle stage such as `cat` blocks forever.
- **Why store `last_pid` rather than take the first failing status?** Bash
  defines a pipeline's status as that of its last stage. `wait_for_all` still
  reaps all children to avoid zombies, but only `last_pid`'s status becomes
  `$?`.
- **Why are heredocs collected before the fork?** They read from the shell's
  real stdin, which the user is typing on. Once forked, a child's stdin is a
  pipe from the previous stage, so it could not read the body. Collecting first
  also makes ctrl-C during a heredoc cancellable in one place.
- **What does collecting before the fork cost, and who pays it?** Every child
  inherits every stage's heredoc read end, not just its own. `apply_redirs`
  consumes only its own, so the rest would survive `execve` and be visible in
  the exec'd program's `/proc/self/fd`. `close_other_heredocs`, called from
  `pipeline_child`, is what pays that cost.
- **Why do the builtin signatures differ?** `echo` and `pwd` need no shell
  state, and norminette flags unused parameters. Uniformity would be cosmetic;
  the uniform part that matters is that they all return an `int` status, so
  `run_builtin` works identically in the parent and in a forked child.
- **Why does `child_exec` never return?** If it did, the child would re-enter
  the fork loop and spawn grandchildren. Every path in it ends in `execve` or
  `exit`.
- **Why does `resolve_command` reject directories explicitly?** Directories
  have the execute bit set as "searchable", so `access(X_OK)` succeeds on them.
  Without the `stat`/`S_ISDIR` check, `/tmp` would be accepted as a command and
  fail later with the wrong exit code.
