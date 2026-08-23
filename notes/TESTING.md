# Testing

Everything lives under `tests/`, and there is one command to run all of it:

```
bash tests/run-all.sh          # everything, including valgrind
bash tests/run-all.sh quick    # skip valgrind, which is the slow stage
```

The harness has five parts of our own — norminette checks, differential edge
cases, interactive pty signal tests, valgrind, and a suppression file — plus
two vendored public testers under `testers/`.

## `tests/run-all.sh` — the single entry point

The only script you should normally run. It does six things in order, and its
defining property is that **a failing stage does not stop the run**: each stage
records its result into a `FAILED` array and the script keeps going, so one
invocation shows the whole picture instead of making you fix one thing at a
time and re-run.

1. **build** — `make -s`. This is the only stage that aborts on failure, since
   nothing downstream can work without a binary.
2. **norminette, globals, forbidden functions** — runs `tests/norm.sh`.
3. **edge cases** — runs `tests/edges.sh`, piping through `tail -n 20` so a
   clean run stays short while failures still show. Note `record
   "${PIPESTATUS[0]}"` rather than `$?`: with a pipe, `$?` would be `tail`'s
   status, which is always 0. Getting that wrong would make every failure
   silently pass.
4. **interactive signals** — runs `tests/signals.py`, guarded by a
   `command -v python3` check because a pty cannot be driven from bash alone.
5. **the two public testers** — each guarded by a file-existence check so the
   run still works on a machine where they were never cloned.
6. **valgrind** — runs `tests/leaks.sh`, skipped entirely when the first
   argument is `quick`.

It changes to the project root first (`cd "$(dirname "$0")/.."`), so it works
from any directory. At the end it prints either the list of failed stage names
or `all stages passed`, and exits non-zero if anything failed.

## `tests/edges.sh` — differential testing against bash, 125 cases

The main correctness suite. Its method is simple and hard to argue with: feed
**the same script** to `./minishell` and to `/bin/bash`, and require that they
agree.

Each case is written as `check <description> <line> [line ...]`, where every
extra argument is one input line. `run_one` pipes those lines into the shell
under test, from inside a fresh temporary directory that is wiped between
runs, so a test that creates files cannot affect the next one. It captures
three things: stdout, the exit code, and whether stderr was non-empty.

The 125 cases are grouped into seven sections:

| Section | Cases | Examples of what it pins down |
| --- | --- | --- |
| quoting | 9 | `''` kept as an argument, adjacent quotes gluing into one word, `'$HOME'` staying literal, a quoted `\|` not being an operator |
| variable expansion | 27 | unset variables vanishing vs. keeping the field when quoted, `$?` after true/false/missing command, word splitting of `$S` vs `"$S"`, the braced `${VAR}` form matching `$VAR` in every quoting context |
| builtins | 37 | `echo -nnn`, `cd -`, `cd --`, `cd ""` as a silent no-op, `export A+=2`, `export 1BAD=x`, `exit 300` wrapping modulo 256, `exit abc` |
| redirections | 22 | append vs. truncate, `> f1 > f2` creating both but writing to the last, heredocs with and without expansion, quoted delimiters |
| pipes | 11 | exit code coming from the last stage, builtins in the middle of a pipeline, `cd` in a pipe not moving the shell, `yes \| head` |
| syntax errors | 10 | leading/trailing pipes, `> >`, unclosed quotes, and whitespace-only lines *not* being errors |
| command resolution | 9 | absolute paths, directories as commands, permission denied, `unset PATH` falling back to the cwd, an empty `PATH` entry |

### Two deliberate testing decisions

**stderr is compared by presence, not by content.** The comparison is
`[[ -s "$WORKDIR/.err" ]]`, giving a 0/1 flag, and only that flag is diffed.
This is intentional. Our diagnostics start with `minishell: ` while bash's
start with `bash: line 3: `, and the subject does not require the messages to
match byte for byte — it requires the *behaviour* to match. Comparing exact
stderr text would fail every single error case for a difference we are
explicitly allowed to have, and would tempt us into faking bash's `line N:`
prefix, which we have no line counter for. Requiring the flag to agree still
catches the real bugs: staying silent when bash reports an error, or printing
an error when bash succeeds. Stdout, by contrast, is compared byte for byte,
because that is data and it must be identical.

**`;` is never used, anywhere in the file.** Command separators are outside the
subject's scope — `is_metachar` in `srcs/utils.c` covers only `|`, `<` and `>`.
That means `echo a;b` is a single ordinary word for us and two commands for
bash, so any test containing a semicolon would report a failure for behaviour
we were never asked to implement. Multi-command tests are expressed instead as
multiple arguments to `check`, one input line each, which is both in scope and
a more faithful model of how the shell is actually used.

Run it alone with `bash tests/edges.sh`. It prints one line per case, dumps the
mini/bash outputs for any mismatch along with which of stdout/exit/stderr
differed, and exits non-zero if anything failed. `MINISHELL=/path/to/binary`
overrides which binary is tested.

## `tests/signals.py` — interactive tests through a pty, 12 cases

Signals cannot be tested by piping. ctrl-C, ctrl-D and ctrl-backslash only
behave as the subject describes when stdin is a **terminal**, and our own
`isatty` gate in `srcs/input.c` deliberately takes a different code path when
it is not. So this suite spawns the shell on a real pseudo-terminal with
`pty.fork()`, which also makes minishell a session leader — meaning a
`killpg` reaches the whole foreground process group exactly as a real ctrl-C
from a keyboard would.

The `Shell` class wraps one pty: `send` writes characters, `read` drains
output with a `select` timeout, `sig` signals the process group, and
`wait_exit` polls `waitpid(WNOHANG)` so a test can distinguish "the shell
exited with status N" from "the shell is still alive". Output is passed through
`strip_ansi` before matching, because readline emits escape sequences for the
prompt.

The twelve cases:

- a prompt is printed on a terminal
- ctrl-C on an empty line redraws the prompt without exiting
- ctrl-C sets `$?` to 130
- ctrl-C discards a half-typed line, so it does not run when you press enter
- ctrl-D on an empty line exits with 0
- ctrl-D exits with the *last* status (`false` then ctrl-D gives 1)
- ctrl-backslash is ignored by the shell itself
- ctrl-C kills a running `sleep 5` and the shell survives with 130
- ctrl-backslash kills a running child with 131
- ctrl-C aborts a heredoc and returns to the prompt
- ctrl-D ends a heredoc without hanging
- **no prompt is printed when stdin is a pipe** — the one case that goes the
  other way, running the shell with `subprocess.run` and asserting stdout is
  exactly `hi\n`. This is the direct test of the `isatty` gate: if readline
  were used on a pipe it would echo the input and this would fail.

Every test is wrapped in a `try`, so an exception is reported as a failure
rather than aborting the run. Run it alone with `python3 tests/signals.py`.

## `tests/leaks.sh` — valgrind

Runs twenty scripts under valgrind and fails if any of them leaks. The flags
that matter:

- `--leak-check=full` with `--show-leak-kinds=definite,indirect` and
  `--errors-for-leak-kinds=definite,indirect`. **Still-reachable memory is not
  treated as an error**, which is the right call for a shell: memory still
  held at exit and freed by the OS is not what the subject asks about.
- `--trace-children=yes`. This is the important one. A leak in the forked child
  between `fork` and `execve` is still our bug, and `execve` replaces the
  process image, so anything allocated and still held at that moment is
  permanently lost. Without tracing children, `env_to_envp`'s array or a
  `resolve_command` result leaked in the child would never be reported.
- `--suppressions=tests/readline.supp` — see below.
- `--error-exitcode=42`, plus a manual scan of the log. The manual scan is
  there because `--error-exitcode` only reflects the **parent** process; the
  traced children write their reports into the same log file but cannot change
  the parent's exit code. So the script greps the log for
  `definitely lost: [1-9]`, `indirectly lost: [1-9]` and
  `Invalid (read|write|free)`, which is what actually catches child leaks.

The twenty scenarios deliberately include the failure paths, not just the happy
ones: a command that is not found, a pipeline containing a missing command, a
failed redirection, four kinds of syntax error, `exit abc`, and — the last
case — reaching EOF without an explicit `exit`, which exercises the
`rl_clear_history` and `env_free` cleanup in `main`.

Run it alone with `bash tests/leaks.sh`. It needs valgrind installed and a
built binary.

## `tests/readline.supp` — valgrind suppressions

Five suppression blocks. `readline` and `ncurses`/`tinfo` allocate internal
buffers on first use and never free them; they are library allocations, not
ours, and the subject explicitly does not hold us responsible for them. They
are matched three ways — by object file (`libreadline.so`, `libtinfo.so`,
`libncurses*.so`) and by the `readline` and `add_history` entry points — so the
suppression works whether or not the library has debug symbols.

Suppressing by library rather than by individual stack trace is a conscious
choice: the traces differ between readline versions and between machines, so
per-trace suppressions would silently stop working on the evaluation machine.
The risk of over-suppressing is accepted because we never allocate anything
that is freed inside readline's own call stack — the one thing we *do* own,
the line returned by `readline()`, is freed by `shell_loop` and is not part of
these blocks. Note we still call `rl_clear_history()` in `main` rather than
suppressing the history list, because that one we genuinely can free.

## `tests/norm.sh` — norminette and the two rules it cannot see

Three checks over `includes srcs path-a path-b`:

1. **norminette** itself. It greps the output for lines starting with `Error`
   and prints them with one line of context; on success it prints how many
   files came back `OK!`. A missing norminette on `PATH` is itself a failure,
   so an evaluation machine without it does not silently look clean.

2. **globals** — the subject allows exactly one. The script greps every `.c`
   file for a line starting at column 0 that ends in `=` or `;`, filters out
   anything containing `(` (which removes function definitions and
   prototypes), and counts what remains. More than one is a failure. The one
   surviving line should be `path-b/signals/signals.c: volatile sig_atomic_t
   g_signal = 0;` — the script prints it even on success, so you can see at a
   glance that the one global is the *right* one. Norminette itself does not
   check this, which is exactly why the check exists here.

3. **forbidden functions** — the allowed list is copied verbatim from the
   subject: readline and its `rl_*` helpers, `printf`, `malloc`, `free`,
   `write`, `access`, `open`, `read`, `close`, `fork`, the `wait` family,
   `signal`/`sigaction`/`sigemptyset`/`sigaddset`, `kill`, `exit`, `getcwd`,
   `chdir`, the `stat` family, `unlink`, `execve`, `dup`, `dup2`, `pipe`, the
   `dir` family, `strerror`, `perror`, `isatty`, `ttyname`, `ttyslot`,
   `ioctl`, `getenv`, `tcsetattr`/`tcgetattr`, and the termcap functions. The
   script extracts every identifier followed by `(` from our sources, then
   filters out three groups: the allowed list, C keywords such as `sizeof` and
   `if` (which look like calls to a regex), and anything we define ourselves or
   that libft provides. Whatever is left is printed as a warning — with an
   explicit note to check it by hand, since the heuristic can produce false
   positives on macros.

This stage is what catches, for example, someone reaching for `isspace` instead
of our own `is_space`, or `strcmp` instead of `ft_strncmp`.

Run it alone with `bash tests/norm.sh`.

## The vendored public testers (`testers/`)

Two well-known community test suites are checked into `testers/`. They are
included because they encode edge cases nobody thinks of on their own, and
because evaluators frequently run one of them during the defence — better to
find the failures first.

**`testers/42_minishell_tester`** (zstenger93). A large suite of command files
under `cmds/`. `run-all.sh` invokes it as
`bash testers/42_minishell_tester/tester.sh m` — the `m` selects the mandatory
part rather than the bonus — and filters the output down to the
`TOTAL TEST COUNT` line, since the full output is thousands of lines. Note the
two oddly named files in that directory, `lol.c"` and `lol.c'`: those are
intentional fixtures with quote characters in their filenames, not mistakes.

**`testers/minishell_tester`** (LucasKuhn). Organised by topic —
`builtins`, `pipes`, `redirects`, `syntax`, `extras`, `wildcards`,
`os_specific` and bonus directories. `run-all.sh` runs it from inside its own
directory (it expects that as its cwd) and extracts the trailing `N/M` score
with `grep -oE "[0-9]+/[0-9]+$"`, then runs `git clean -fq` afterwards because
the tester leaves generated files behind and we do not want them showing up as
project changes.

Both invocations are guarded by an existence check on the tester's entry
script, so `run-all.sh` degrades gracefully to `not installed` on a machine
where they were not cloned rather than failing the whole run. Their results are
reported but deliberately **not** recorded into `run-all.sh`'s `FAILED` array:
they cover bonus features and behaviours outside the subject's scope, so a
non-perfect score there is informative rather than a build failure. Our own
`edges.sh`, `signals.py`, `norm.sh` and `leaks.sh` are the ones that gate.

## Quick reference

| Command | What it runs |
| --- | --- |
| `bash tests/run-all.sh` | Everything, including valgrind |
| `bash tests/run-all.sh quick` | Everything except valgrind |
| `bash tests/norm.sh` | Norminette, the global count, the forbidden-function scan |
| `bash tests/edges.sh` | The 125 differential cases against bash |
| `python3 tests/signals.py` | The 12 interactive pty cases |
| `bash tests/leaks.sh` | The 20 valgrind scenarios |

All four standalone scripts honour `MINISHELL=/path/to/binary` if you want to
test something other than `./minishell`.
