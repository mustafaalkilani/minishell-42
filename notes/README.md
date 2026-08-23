# minishell notes

Study notes for the whole project. The code itself is kept comment-light
because norminette penalises clutter, so all the explanation lives here. The
`notes/` tree mirrors the source tree exactly: a file at `X/foo.c` is explained
in `notes/X/foo.md`.

## How the shell works

The pipeline is always the same four stages — **read line -> LEX -> EXPAND ->
PARSE -> EXECUTE** — followed by cleanup. Here is what happens to the line
`cat < in | grep x > out`, from the keystroke to the exit code, naming the
function responsible at each step so you can jump straight to the right notes
file.

1. **Get the line.** `shell_loop` (`srcs/main.c`) first calls
   `signals_setup_interactive` (`path-b/signals/signals.c`) so ctrl-C at the
   prompt redraws instead of killing the shell, then calls `read_input_line`
   (`srcs/input.c`). That function checks `isatty(STDIN_FILENO)`: on a terminal
   it uses `readline(PROMPT)` for editing and history; on a pipe it falls back
   to `read_plain_line`, which reads one byte at a time so no input past the
   newline is stolen from a child. The returned string is
   `cat < in | grep x > out` with no trailing newline.

2. **Skip blanks and record history.** `shell_loop` calls `add_history` for a
   non-empty line, then `process_line`, which calls `line_is_blank`
   (`srcs/utils.c`). Our line is not blank, so it continues. This early filter
   is what makes a later `NULL` unambiguously mean "syntax error".

3. **LEX.** `build_cmds` (`srcs/main.c`) calls `lex`
   (`path-a/lexer/lexer.c`). `next_token` skips whitespace and dispatches on
   `is_metachar` (`srcs/utils.c`): `read_operator`
   (`path-a/lexer/lexer_ops.c`) produces the `<`, `|` and `>` tokens, testing
   two-character forms first so `>>` is never split; `read_word` runs the
   two-pass `word_measure` / `word_fill` pair
   (`path-a/lexer/lexer_quotes.c`) to strip quotes and record a `quotes[]`
   byte per character. Result: eight tokens —
   `cat`, `<`, `in`, `|`, `grep`, `x`, `>`, `out`.

4. **EXPAND.** `build_cmds` calls `expand`
   (`path-a/expander/expander.c`) **before** parsing, because the per-character
   quote metadata only exists while the tokens do. `expand_masked` walks each
   word building `out` and a parallel `mask`; `split_token`
   (`path-a/expander/expander_split.c`) then splits on whitespace that the mask
   marks as coming from an unquoted expansion. This line has no `$` and no
   quotes, so every token survives unchanged — but the same machinery is what
   makes `$S` split into three arguments while `"$S"` stays one. Heredoc
   delimiters are skipped here on purpose.

5. **PARSE.** `build_cmds` calls `parse` (`path-a/parser/parser.c`), then frees
   the token list. `parse_step` appends words with `cmd_add_arg`
   (`path-a/parser/parser_utils.c`) and hands redirection operators to
   `parse_redir_step` (`path-a/parser/parser_redir.c`), which checks that a
   word follows and calls `cmd_add_redir`. `parse_pipe` starts a new `t_cmd`
   at each `|`. Result: two `t_cmd` nodes — `{argv=["cat"], redirs=[R_IN "in"]}`
   linked to `{argv=["grep","x"], redirs=[R_OUT "out"]}`.

6. **EXECUTE — heredocs first.** `process_line` calls `exec_line`
   (`path-b/executor/exec_line.c`), which starts with `collect_heredocs`
   (`path-b/redirs/redir_heredoc.c`). There are none here, but this must run
   before any fork because a heredoc reads the shell's real stdin. `exec_line`
   then checks whether this is a lone builtin (it is not — there is a `next`),
   and falls through to `run_pipeline`.

7. **Fork the pipeline.** `run_pipeline` calls `signals_ignore`
   (`path-b/signals/signals_child.c`) so ctrl-C reaches only the children, then
   loops: `pipe()` for the `cat`->`grep` connection, `fork()`, and in the child
   `pipeline_child` wires `pipefd[1]` onto stdout and closes everything else,
   while `pipeline_parent` closes the write end and carries `pipefd[0]` forward
   as the next iteration's `prev_read`. The fd state lives in `t_pipeline`
   (`path-b/path_b.h`).

8. **Each child becomes the command.** `child_exec`
   (`path-b/executor/exec_child.c`) calls `signals_setup_child` to restore
   `SIG_DFL`, then `apply_redirs` (`path-b/redirs/redir_apply.c`), which opens
   `in` read-only and dups it onto `cat`'s stdin, and opens `out` with
   `O_CREAT|O_TRUNC` and dups it onto `grep`'s stdout. Neither `cat` nor `grep`
   is a builtin, so `resolve_command` (`path-b/executor/exec_path.c`) walks
   `PATH` to find `/usr/bin/cat`, `env_to_envp` (`path-b/env/env_export.c`)
   flattens the environment list back into a `char **`, and `execve` replaces
   the process. It never returns; `execve_failed` handles the failure codes.

9. **Wait and compute `$?`.** Back in the parent, `wait_for_all`
   (`path-b/executor/exec_wait.c`) reaps every child so none becomes a zombie,
   but only the pid matching `last_pid` — `grep` — sets the status, via
   `status_to_exit`. That is bash's rule: a pipeline's status is its last
   stage's.

10. **Store and clean up.** `process_line` assigns the result to
    `sh->last_status`, which is what `$?` will expand to on the next line, and
    calls `free_cmds` to release the whole `t_cmd` list including any open
    heredoc fds.

11. **Exit.** The loop repeats. On ctrl-D, `read_input_line` returns `NULL`,
    `shell_loop` prints `exit` to stderr (only when `isatty`, so piped output
    stays clean) and breaks. `main` then calls `env_free` and
    `rl_clear_history` and returns `sh->last_status` as the process exit code.

The one global in the whole project, `volatile sig_atomic_t g_signal`, is
defined in `path-b/signals/signals.c`. Handlers may only write a signal number
into it; `shell_loop` and `read_heredoc` read it and translate it into an exit
status themselves.

## Contents

### Entry point and shared glue (`srcs/`)

| Notes | Source | What it covers |
| --- | --- | --- |
| [srcs/main.md](srcs/main.md) | `srcs/main.c` | `main`, shell init, the read-eval-print loop, and the lex/expand/parse/execute sequence |
| [srcs/input.md](srcs/input.md) | `srcs/input.c` | The `isatty` gate between `readline` and the byte-at-a-time fallback reader |
| [srcs/utils.md](srcs/utils.md) | `srcs/utils.c` | `shell_error`'s bash-shaped diagnostics, `is_space`, `is_metachar`, `line_is_blank` |

### Shared header (`includes/`)

| Notes | Source | What it covers |
| --- | --- | --- |
| [includes/minishell.md](includes/minishell.md) | `includes/minishell.h` | Every shared type, the quote bit-packing macros, the exit-status constants, the one global, and the public prototypes of both halves |

### path-a: lexer, parser, expander

| Notes | Source | What it covers |
| --- | --- | --- |
| [path-a/path_a.md](path-a/path_a.md) | `path-a/path_a.h` | The `t_exp` accumulator and its split mask, plus every path-a internal prototype |
| [path-a/lexer/lexer.md](path-a/lexer/lexer.md) | `path-a/lexer/lexer.c` | The main scan loop: `lex`, `next_token`, `read_word` |
| [path-a/lexer/lexer_ops.md](path-a/lexer/lexer_ops.md) | `path-a/lexer/lexer_ops.c` | Operator tokens, with `<<` and `>>` matched before `<` and `>` |
| [path-a/lexer/lexer_quotes.md](path-a/lexer/lexer_quotes.md) | `path-a/lexer/lexer_quotes.c` | The quote state machine and the two-pass `word_measure`/`word_fill` that builds `quotes[]` |
| [path-a/lexer/lexer_utils.md](path-a/lexer/lexer_utils.md) | `path-a/lexer/lexer_utils.c` | Token allocation, list append, and `free_tokens` |
| [path-a/parser/parser.md](path-a/parser/parser.md) | `path-a/parser/parser.c` | `parse`, the pipe/word step loop, `syntax_error` and `free_cmds` |
| [path-a/parser/parser_redir.md](path-a/parser/parser_redir.md) | `path-a/parser/parser_redir.c` | Building `t_redir` nodes, including `quoted_delim` for heredocs |
| [path-a/parser/parser_utils.md](path-a/parser/parser_utils.md) | `path-a/parser/parser_utils.c` | `t_cmd` allocation, growing `argv`, and `free_redirs` closing heredoc fds |
| [path-a/expander/expander.md](path-a/expander/expander.md) | `path-a/expander/expander.c` | `expand`, `expand_word`, and the `$`-substitution walk that builds text and mask together, including the `${VAR}` / `$VAR` dispatch |
| [path-a/expander/expander_split.md](path-a/expander/expander_split.md) | `path-a/expander/expander_split.c` | Field splitting driven by the mask, and splicing extra tokens into the list |
| [path-a/expander/expander_prefix.md](path-a/expander/expander_prefix.md) | `path-a/expander/expander_prefix.c` | Tilde expansion, the `$"..."` quote-prefix rule, and the `${NAME}` / `${?}` brace form |
| [path-a/expander/expander_utils.md](path-a/expander/expander_utils.md) | `path-a/expander/expander_utils.c` | Variable name scanning, `$?` lookup, and the `join_free`/`pad_new`/`exp_append` allocation core |

### path-b: executor, builtins, env, redirections, signals

| Notes | Source | What it covers |
| --- | --- | --- |
| [path-b/path_b.md](path-b/path_b.md) | `path-b/path_b.h` | The `t_pipeline` fd-threading struct and every path-b internal prototype |
| [path-b/executor/exec_line.md](path-b/executor/exec_line.md) | `path-b/executor/exec_line.c` | `exec_line`, the fork/pipe loop, and running a lone builtin in the parent |
| [path-b/executor/exec_child.md](path-b/executor/exec_child.md) | `path-b/executor/exec_child.c` | What the forked child does before `execve`, and the 126/127 failure codes |
| [path-b/executor/exec_path.md](path-b/executor/exec_path.md) | `path-b/executor/exec_path.c` | `PATH` lookup, the `/` shortcut, the empty-`PATH` fallback, and rejecting directories |
| [path-b/executor/exec_wait.md](path-b/executor/exec_wait.md) | `path-b/executor/exec_wait.c` | Reaping every child, taking `$?` from the last one, and the 128+signal convention |
| [path-b/redirs/redir_apply.md](path-b/redirs/redir_apply.md) | `path-b/redirs/redir_apply.c` | Opening targets with the right flags and applying redirections left to right |
| [path-b/redirs/redir_heredoc.md](path-b/redirs/redir_heredoc.md) | `path-b/redirs/redir_heredoc.c` | Draining heredoc bodies into a pipe before any fork, and aborting on ctrl-C |
| [path-b/signals/signals.md](path-b/signals/signals.md) | `path-b/signals/signals.c` | The `g_signal` global, the SIGINT handlers, and the interactive/heredoc setups |
| [path-b/signals/signals_child.md](path-b/signals/signals_child.md) | `path-b/signals/signals_child.c` | Resetting to `SIG_DFL` in the child and ignoring signals in the parent while a child runs |
| [path-b/env/env_init.md](path-b/env/env_init.md) | `path-b/env/env_init.c` | Building the `t_env` list from `envp` and bumping `SHLVL` |
| [path-b/env/env_ops.md](path-b/env/env_ops.md) | `path-b/env/env_ops.c` | Find, get, set in place, unset, and free |
| [path-b/env/env_export.md](path-b/env/env_export.md) | `path-b/env/env_export.c` | Flattening back to `char **` for `execve`, identifier validation, and `+=` |
| [path-b/builtins/builtin_dispatch.md](path-b/builtins/builtin_dispatch.md) | `path-b/builtins/builtin_dispatch.c` | `is_builtin` and `run_builtin`, the single entry point to all seven |
| [path-b/builtins/builtin_echo.md](path-b/builtins/builtin_echo.md) | `path-b/builtins/builtin_echo.c` | `echo` and the stacked `-n` flag rules |
| [path-b/builtins/builtin_cd.md](path-b/builtins/builtin_cd.md) | `path-b/builtins/builtin_cd.c` | `cd`, `HOME`, `cd -`, `cd ""` as a no-op, and updating `PWD`/`OLDPWD` |
| [path-b/builtins/builtin_pwd.md](path-b/builtins/builtin_pwd.md) | `path-b/builtins/builtin_pwd.c` | `pwd` via `getcwd` |
| [path-b/builtins/builtin_env.md](path-b/builtins/builtin_env.md) | `path-b/builtins/builtin_env.c` | `env` printing only variables that have a value |
| [path-b/builtins/builtin_export.md](path-b/builtins/builtin_export.md) | `path-b/builtins/builtin_export.c` | `export` with arguments, validation, and `+=` |
| [path-b/builtins/builtin_export_list.md](path-b/builtins/builtin_export_list.md) | `path-b/builtins/builtin_export_list.c` | `export` with no arguments: the sorted `declare -x` listing |
| [path-b/builtins/builtin_unset.md](path-b/builtins/builtin_unset.md) | `path-b/builtins/builtin_unset.c` | `unset`, and why an invalid identifier is not an error |
| [path-b/builtins/builtin_exit.md](path-b/builtins/builtin_exit.md) | `path-b/builtins/builtin_exit.c` | `exit`, numeric argument parsing, modulo 256, and cleanup before leaving |

### Testing

| Notes | What it covers |
| --- | --- |
| [TESTING.md](TESTING.md) | The `tests/` harness: `run-all.sh`, the differential edge cases, the pty signal tests, valgrind, norminette, and the two vendored public testers |

Some path-a and path-b notes files are written by separate passes and may not
all exist yet; the table above is derived from the source tree, so any missing
link corresponds to a file that has not been written yet rather than a source
file that does not exist.
