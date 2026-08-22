# minishell.h

The one header both halves of the project include. It pulls in every system
header the subject allows, defines the exit-status constants, declares the
five data structures that flow through the pipeline (`t_token` -> `t_cmd`,
plus `t_env` and `t_shell`), declares the single permitted global, and lists
the public entry points of path-a and path-b. Anything private to one half
lives in `path-a/path_a.h` or `path-b/path_b.h` instead; if a symbol is in
this file, it crosses the boundary between the two partners.

## Walkthrough

### Include guard and system headers

```c
#ifndef MINISHELL_H
# define MINISHELL_H
```
Standard double-inclusion guard. The `#` is at column 0 and the directive is
indented one space after it — that is norminette's rule for nested
preprocessor directives, and every `# include` and `# define` below follows
it.

```c
# include "../libft/libft.h"
```
First, so everything below can use `ft_strdup`, `ft_putstr_fd`, `ft_split` and
friends. The relative path works because the Makefile adds `includes/` to the
include search path, making `../libft` resolve from this file's own directory.

The system headers, and what each one is actually for:

- `stdio.h` — only for `printf`, which the subject allows. Note that we
  deliberately do not use it for error output; that goes through `write` in
  `shell_error`.
- `stdlib.h` — `malloc`, `free`, `exit`, `getenv`.
- `unistd.h` — the core syscall wrappers: `read`, `write`, `close`, `fork`,
  `execve`, `dup`, `dup2`, `pipe`, `access`, `chdir`, `getcwd`, `isatty`.
- `string.h` — `strerror` only, used to reproduce the system's exact wording
  in redirection and `cd` errors.
- `fcntl.h` — `open` and the `O_RDONLY` / `O_WRONLY` / `O_CREAT` / `O_TRUNC` /
  `O_APPEND` flags used by `redir_apply.c`.
- `errno.h` — `errno`, read immediately after a failed `open` or `chdir` and
  passed to `strerror`.
- `signal.h` — `sigaction`, `sigemptyset`, `kill`, `SIGINT`, `SIGQUIT`, and
  the `sig_atomic_t` type used by the global.
- `sys/types.h` — `pid_t`, the type of everything returned by `fork`.
- `sys/wait.h` — `waitpid` and the `WIFEXITED` / `WEXITSTATUS` / `WIFSIGNALED`
  / `WTERMSIG` macros that `status_to_exit` uses.
- `sys/stat.h` — `stat` and `S_ISDIR`, so `resolve_command` can tell a
  directory from an executable and return 126 rather than trying to `execve`
  it.
- `dirent.h` and `termios.h` — allowed by the subject and included for
  completeness of the toolchain the project is permitted to use.
- `readline/readline.h`, `readline/history.h` — `readline`, `add_history`,
  `rl_clear_history` and the `rl_*` redisplay functions.

Including everything in one header rather than per-file is a norminette-driven
choice: it keeps each `.c` file down to a single `#include` line and removes
any chance of a file compiling on one machine and not another because of a
missing transitive include.

### `# define PROMPT "minishell$ "`

The interactive prompt, with a trailing space so the cursor does not sit
against the `$`. It is a macro rather than a literal at the call site so the
prompt string exists exactly once; `shell_loop` passes it to
`read_input_line`, which drops it entirely when stdin is not a tty.

### Exit-status constants

```c
# define EXIT_OK          0
# define EXIT_MISUSE      2
# define EXIT_CANT_EXEC   126
# define EXIT_NOT_FOUND   127
# define EXIT_SIG_BASE    128
```
These are bash's conventions, not arbitrary numbers, and each is used in a
specific place:

- `EXIT_OK` (0) — success.
- `EXIT_MISUSE` (2) — syntax errors, and builtin misuse such as
  `exit abc`. Returned by `process_line` when `build_cmds` yields NULL.
- `EXIT_CANT_EXEC` (126) — the command was found but could not be run:
  permission denied, or the target is a directory. `resolve_command` and
  `execve_failed` produce this.
- `EXIT_NOT_FOUND` (127) — no such command anywhere on `PATH`.
- `EXIT_SIG_BASE` (128) — the base for signal deaths, added to the signal
  number: 128 + SIGINT(2) = 130, 128 + SIGQUIT(3) = 131. Used in `shell_loop`
  for an interrupted prompt, in `exec_line` when a heredoc is aborted, and in
  `status_to_exit` when a child was killed.

Naming them is what lets the executor read as `EXIT_SIG_BASE + SIGINT` instead
of a bare `130`, so the intent survives the code review.

### `typedef enum e_quote`

```c
typedef enum e_quote
{
    Q_NONE = 0,
    Q_SINGLE = 1,
    Q_DOUBLE = 2
}   t_quote;
```
The quote state of a single character, decided by the lexer and consumed by
the expander. The values are pinned explicitly (0, 1, 2) rather than left
implicit because they are packed into a byte alongside a flag bit — see
`Q_MASK` below — so their numeric values are part of the contract, not an
implementation detail.

The three states drive three different behaviours: `Q_SINGLE` means a `$` is
literal data and nothing expands; `Q_DOUBLE` means `$` expands but the result
must not word-split; `Q_NONE` means `$` expands and the result *does* split on
whitespace. That is why the state must be tracked per character rather than
per token: `"$A"$B` has two different regimes inside one word.

### `# define Q_MASK 3` and `# define Q_BREAK 4`

```c
# define Q_MASK  3
# define Q_BREAK 4
```
Each byte of a token's `quotes[]` array packs two pieces of information:

- bits 0-1 (`Q_MASK` = binary `11`) hold the `t_quote` value. Extracted with
  `quotes[i] & Q_MASK`, which is the idiom used all through the expander.
- bit 2 (`Q_BREAK` = binary `100`) is a flag meaning "a quote delimiter was
  stripped immediately before this character".

`Q_BREAK` is the subtle one and it is worth being able to explain unprompted.
Once the lexer removes the quote characters themselves, `$T"o"` and `$To`
become the same seven-then-two-character run of text — but they must expand
differently. In `$T"o"` the variable name is `T` and `o` is a separate literal;
in `$To` the variable name is `To`. The stripped `"` left no character behind
to stop the name scan, so we record its former presence as `Q_BREAK` on the
`o`. `var_name_len` stops when it sees that bit, and `is_quote_prefix` uses the
same bit to detect `$"HOME"`, where the `$` is dropped entirely.

Packing both into one byte rather than using two parallel arrays halves the
allocation and the copying, and keeps the array the same length as `value` so
`quotes[i]` always describes `value[i]`.

### `typedef enum e_token_type`

```c
typedef enum e_token_type
{
    T_WORD,
    T_PIPE,
    T_REDIR_IN,
    T_REDIR_OUT,
    T_APPEND,
    T_HEREDOC
}   t_token_type;
```
The complete token vocabulary: one word type and the five operators the
subject requires (`|`, `<`, `>`, `>>`, `<<`). There is no `T_SEMICOLON` and no
`T_AND`/`T_OR` because separators and logical operators are out of scope — the
enum is a direct statement of the grammar we implement.

`T_WORD` is first so it gets value 0, which makes a zeroed token default to
the common case.

### `typedef struct s_token`

```c
typedef struct s_token
{
    t_token_type    type;
    char            *value;
    char            *quotes;
    int             had_quotes;
    struct s_token  *next;
}   t_token;
```
The lexer's output unit, a singly linked list.

- `type` — which of the six kinds this is. For operators, `value` still holds
  the literal text (useful for the `syntax error near unexpected token` message).
- `value` — the token's text with quote characters already removed.
- `quotes` — the parallel byte array described above, exactly `ft_strlen(value)`
  bytes plus a terminator, so `quotes[i]` always describes `value[i]`. A
  parallel array rather than an array of structs keeps it a plain string that
  `ft_substr` and friends can slice alongside `value`.
- `had_quotes` — a per-token flag, not per-character, recording that the
  original text contained at least one quote **anywhere**. It survives
  expansion, and the parser uses it to answer one question: when a word
  expands to nothing, do we drop it or keep an empty argument? `echo $NOPE`
  passes one argument (bash drops the empty field), while `echo "$NOPE"`
  passes two (the quotes force an empty field to exist). Per-character quote
  state cannot answer this after expansion has already emptied the word, which
  is why the flag is separate and token-level.
- `next` — singly linked because every consumer walks the list forward exactly
  once; nothing ever needs to go backwards.

A linked list rather than an array because `split_token` inserts new tokens in
the middle when an unquoted expansion word-splits, and that is O(1) on a list.

### `typedef enum e_redir_type`

```c
typedef enum e_redir_type
{
    R_IN,
    R_OUT,
    R_APPEND,
    R_HEREDOC
}   t_redir_type;
```
A second enum covering the same four redirection operators as the token enum.
The duplication is deliberate: tokens are a lexical concept and redirections
are a semantic one, and keeping the types distinct means the executor never
accidentally receives a `T_PIPE` where a redirection is expected. The compiler
enforces the separation.

### `typedef struct s_redir`

```c
typedef struct s_redir
{
    t_redir_type    type;
    char            *filename;
    int             quoted_delim;
    int             heredoc_fd;
    struct s_redir  *next;
}   t_redir;
```
One redirection attached to one command.

- `type` — which operator, selecting the `open` flags in `open_target` or the
  heredoc path in `apply_one`.
- `filename` — overloaded on purpose: the target path for `<`, `>` and `>>`,
  and the **delimiter** for `<<`. One field instead of a union keeps the
  struct and its cleanup trivial, and the `type` field always says which
  meaning applies.
- `quoted_delim` — set by the parser when the heredoc delimiter had any quotes
  (`<< "EOF"` or `<< 'EOF'`). Bash's rule: a quoted delimiter means the body is
  taken literally, with no `$` expansion. It has to be recorded at parse time
  because the quote metadata is gone by the time the body is read.
- `heredoc_fd` — the read end of the pipe the body was written into. Heredocs
  are drained **before** any fork, so by execution time there is no file to
  open; `apply_one` just `dup2`s this fd onto stdin. Initialised to -1 and
  reset to -1 after use so a double-apply cannot close an fd twice.
- `next` — redirections are a list because a command may have many, and they
  must be applied strictly left to right (`> a > b` creates both but leaves
  stdout on `b`).

### `typedef struct s_cmd`

```c
typedef struct s_cmd
{
    char            **argv;
    t_redir         *redirs;
    struct s_cmd    *next;
}   t_cmd;
```
One segment of a pipeline — the parser's output and the executor's input.

- `argv` — NULL-terminated, because that is exactly what `execve` demands.
  Building it in the required shape means the executor can pass
  `cmd->argv` straight through with no conversion step.
- `redirs` — this segment's own redirection list; each segment redirects
  independently.
- `next` — the next stage of the pipeline. A list, not a tree: the grammar the
  subject defines is flat (`cmd | cmd | cmd`), with no `&&`, `||` or
  subshells, so a list is the exact right shape and a tree would be
  over-engineering. `!cmds->next` is the executor's test for "single command",
  which is what lets a lone builtin run in the parent process.

### `typedef struct s_env`

```c
typedef struct s_env
{
    char            *key;
    char            *value;
    struct s_env    *next;
}   t_env;
```
The environment as a linked list of split key/value pairs rather than the
`char **` of `KEY=VALUE` strings the kernel hands us.

Splitting at startup pays for itself immediately: `env_get` compares only the
key with no scanning for `=`; `env_set` replaces one field without rebuilding
a string; `env_unset` unlinks a node; `export A+=1` appends to `value` in
place. A `char **` would require reallocating the whole array for every insert
and delete.

`value` can be NULL, and that is meaningful, not an error state: `export
ONLYNAME` creates a variable that exists for `export`'s listing but is **not**
placed in the child's environment. `env_to_envp` skips NULL-valued nodes,
which is exactly the behaviour `tests/edges.sh` checks with
`export ONLYNAME` followed by `env | grep -c ONLYNAME`.

### `typedef struct s_shell`

```c
typedef struct s_shell
{
    t_env   *env;
    int     last_status;
    int     stdin_backup;
    int     stdout_backup;
    t_cmd   *current_cmds;
    char    *current_line;
}   t_shell;
```
All mutable shell state in one struct, allocated on `main`'s stack and passed
by pointer to everything. This is the mechanism by which the project obeys the
"one global variable" rule: the state that a lesser design would make global
is instead an explicit parameter.

- `env` — the environment list, mutated by `export`, `unset` and `cd`.
- `last_status` — `$?`. Read by the expander when it sees `$?`, written by
  `process_line` after every command and by `shell_loop` after an interrupted
  prompt.
- `stdin_backup` / `stdout_backup` — saved descriptors for restoring the
  shell's own standard streams after a builtin ran with redirections. Both
  start at -1 meaning "nothing saved", so a stale `close()` cannot shut fd 0.
- `current_cmds` — a handle on the pipeline being executed, set at the top of
  `exec_line`, so cleanup paths can reach the command list without it being
  threaded through every helper.
- `current_line` — the same idea one level further out: the readline buffer for
  the line currently being run, set by `shell_loop` right after it is read and
  cleared to NULL right after it is freed. It exists for exactly one caller.
  The `exit` builtin calls `exit()` from deep inside `process_line` and never
  returns, so `shell_loop`'s own `free(line)` is skipped and the input string for
  the line that said `exit` would stay allocated until the process died.
  `shell_cleanup` frees this field first, which is what makes `exit` a complete
  teardown rather than one that leaves a block behind. Ownership still belongs to
  `shell_loop`; this is a borrowed alias, which is why the loop nulls it out the
  moment the real owner releases it.

Both pointer fields are initialised to NULL by `shell_init`, so no cleanup path
can read stack garbage even if it runs before `exec_line` ever assigned anything.

### `extern volatile sig_atomic_t g_signal;`

```c
extern volatile sig_atomic_t    g_signal;
```
The one and only global the subject permits, declared here and defined in
`path-b/signals/signals.c`. `tests/norm.sh` counts globals across all `.c`
files and fails the build if there is more than one.

Every part of the declaration is load-bearing:

- `volatile` — the value is changed asynchronously by a signal handler. Without
  it, the compiler is entitled to cache `g_signal` in a register across the
  `readline` call and the main loop would never observe the change.
- `sig_atomic_t` — the only type the C standard guarantees can be read and
  written atomically with respect to signal delivery. Anything wider could be
  seen half-updated if a signal arrived mid-store.
- It holds **only a signal number**. No pointers, no struct, no shell state.
  That restriction is the subject's, and it is the reason `shell_loop` has to
  translate `g_signal == SIGINT` into `last_status = 130` afterwards rather
  than the handler doing it: a handler must not touch `t_shell`.

Readers are `shell_loop` (to set 130 after an interrupted prompt) and
`read_heredoc` (to abort the heredoc); writers are the two handlers in
`signals.c`. Both readers clear it after consuming it, so one ctrl-C affects
exactly one thing.

### Prototype groups

The prototypes are ordered by subsystem, and the grouping is itself
documentation of the architecture.

**path-a public entry points** — the front end, in pipeline order:
```c
t_token *lex(const char *input);
int     expand(t_token *tokens, t_shell *sh);
t_cmd   *parse(t_token *tokens);
void    free_tokens(t_token *tokens);
void    free_cmds(t_cmd *cmds);
```
Text becomes tokens, tokens are expanded in place, expanded tokens become
commands. The order in the header mirrors the order `build_cmds` calls them,
and the comment above them states why expansion must precede parsing: the
per-character quote metadata only exists while the tokens do.

**The shared expander entry point:**
```c
char    *expand_word(const char *value, const char *quotes, t_shell *sh);
```
The one function that crosses the boundary in the unusual direction — path-b
calling into path-a. Heredoc bodies obey exactly the same expansion rules as
command words, so `redir_heredoc.c` reuses this rather than reimplementing
`$` handling. It differs from `expand` in throwing away the split mask,
because a heredoc line is never split into fields.

**path-b, environment:**
```c
t_env   *env_init(char **envp);
char    *env_get(t_env *env, const char *key);
int     env_set(t_env **env, const char *key, const char *value);
int     env_unset(t_env **env, const char *key);
char    **env_to_envp(t_env *env);
void    env_free(t_env *env);
```
Owns the `t_env` list end to end: build it from `envp`, query it (the
expander's `var_lookup` and `cd`'s `HOME` lookup go through `env_get`), mutate
it, flatten it back into the `char **` that `execve` needs, and free it. Note
the `t_env **` double pointers on `set`/`unset` — they can change the head of
the list.

**path-b, builtins:**
```c
int     is_builtin(const char *name);
int     run_builtin(t_cmd *cmd, t_shell *sh);
```
Only two symbols are public. `is_builtin` is what `exec_line` asks before
deciding to run a single command in the parent process instead of forking —
essential, because `cd`, `export` and `unset` must change the parent's state
to have any effect. The seven individual builtin functions stay private in
`path_b.h`.

**path-b, execution:**
```c
int     exec_line(t_cmd *cmds, t_shell *sh);
```
A single entry point for the entire back end. Everything behind it — heredoc
collection, the fork/pipe loop, path resolution, waiting — is private. The
narrow interface is why the two halves could be written in parallel.

**path-b, signals:**
```c
void    signals_setup_interactive(void);
void    signals_setup_child(void);
void    signals_setup_heredoc(void);
void    signals_ignore(void);
```
Four named states rather than one function taking a mode argument, so every
call site reads as a statement of intent. Interactive is the prompt (SIGINT
redraws, SIGQUIT ignored); child resets both to `SIG_DFL` before `execve` so
the child dies normally; heredoc uses a handler that aborts the body; ignore
is the parent while a foreground child runs, so ctrl-C reaches only the child.

**Shared helpers:**
```c
void    shell_error(const char *ctx, const char *arg, const char *msg);
int     is_metachar(char c);
int     is_space(char c);
int     line_is_blank(const char *line);
char    *read_input_line(const char *prompt);
```
The contents of `srcs/utils.c` and `srcs/input.c` — the handful of functions
that belong to neither partner and are called from both.

## Things to be ready to explain

- **Why is the quote state per character instead of per token?** Because one
  word can mix regimes: in `"$A"$B` the first expansion must not word-split
  and the second must. Only a per-character array can express that.
- **What is `Q_BREAK` for?** It records that a quote delimiter was stripped
  just before that character. Without it `$T"o"` and `$To` would be
  indistinguishable after quote removal, and the variable name scan would run
  past the boundary. `var_name_len` and `is_quote_prefix` both test this bit.
- **Why does `had_quotes` exist when `quotes[]` already records quoting?**
  Because it must survive expansion. After `"$NOPE"` expands to nothing there
  is no character left to carry a quote byte, yet the parser still has to emit
  an empty argument — `had_quotes` is what tells it to.
- **Why is `t_env` a linked list instead of `char **`?** So `export` and
  `unset` are O(1) node operations instead of reallocating an array, and so
  lookups compare a key field directly instead of scanning for `=`. A NULL
  `value` also lets us model `export ONLYNAME`, which exists but is not
  exported.
- **Why `volatile sig_atomic_t` and why only a number?** `volatile` stops the
  compiler caching the value across the `readline` call; `sig_atomic_t` is the
  only type guaranteed atomic against signal delivery; and the subject forbids
  storing anything richer, which is why the main loop, not the handler,
  converts SIGINT into exit status 130.
- **Why is the pipeline a list and not a tree?** The grammar in scope is flat:
  `cmd | cmd | cmd` with no `&&`, `||` or subshells. A list matches it exactly,
  and `!cmds->next` is the single test that identifies a lone command eligible
  to run as a builtin in the parent.
- **Why does `t_redir` reuse `filename` for the heredoc delimiter?** One field
  plus the `type` tag is simpler to allocate and free than a union, and no code
  path can be confused about which meaning applies because `type` is always
  checked first.
- **Why do `current_cmds` and `current_line` live in `t_shell` at all?** Because
  `exit` leaves the program with `exit()` from the middle of the call stack, so
  every function above it is skipped and their frees never run. Parking the two
  live allocations on the struct that `builtin_exit` already receives lets
  `shell_cleanup` reach them without threading extra parameters through
  `process_line`, `build_cmds` and `run_builtin`. Both are borrowed aliases, not
  owned pointers, and both are nulled by their real owner after freeing.
