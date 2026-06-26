*This project has been created as part of the 42 curriculum by malkilan, rabdalqa.*

# minishell

> _As beautiful as a shell._

A minimal, POSIX-style shell written in C — our very own little **Bash**.
It reads a command line, parses it (quotes, expansions, redirections, pipes), and executes the resulting pipeline of built-ins or external programs, all while behaving the way an interactive terminal should.

---

## Description

`minishell` is a from-scratch reimplementation of the core features of a Unix shell, built as part of the 42 curriculum (subject version **10.0**).
The goal is to gain hands-on, low-level knowledge of **processes**, **file descriptors**, **signals**, and **terminal I/O** by writing a program that:

- displays an interactive prompt and reads input through `readline`,
- maintains a working command **history**,
- searches for and launches the right executable using the `PATH` environment variable, or a relative/absolute path,
- handles single (`'`) and double (`"`) quotes correctly,
- expands environment variables (`$VAR`) and the special `$?` (last exit status),
- implements the redirections `<`, `>`, `<<` (heredoc), and `>>`,
- implements pipelines (`|`) of arbitrary length,
- implements the built-ins `echo -n`, `cd`, `pwd`, `export`, `unset`, `env`, and `exit`,
- behaves correctly on `ctrl-C`, `ctrl-D`, and `ctrl-\` in interactive mode,
- uses **at most one global variable**, which only stores a received signal number.

Anything outside of what the subject requires (e.g. `;`, `\`, unclosed quotes, wildcards, `&&`/`||`) is **not** interpreted in the mandatory part.

---

## Instructions

### Requirements

- a Unix-like system (Linux or macOS),
- `cc` (clang or gcc) with support for `-Wall -Wextra -Werror`,
- GNU `make`,
- the **readline** library and its development headers.

Install `readline` if you don't already have it:

```bash
# Debian / Ubuntu
sudo apt install libreadline-dev

# macOS (Homebrew)
brew install readline
```

### Build

Clone the repository and build with `make`:

```bash
git clone <this-repo-url> minishell
cd minishell
make
```

Available Makefile rules:

| rule      | what it does                                       |
|-----------|----------------------------------------------------|
| `all`     | (default) build the `minishell` binary             |
| `clean`   | remove object files                                |
| `fclean`  | remove object files **and** the `minishell` binary |
| `re`      | `fclean` then `all`                                |

The build uses `-Wall -Wextra -Werror` and re-links only when necessary.

### Run

```bash
./minishell
```

You'll get an interactive prompt. Try things like:

```bash
$ echo "hello, $USER" | cat -e
$ ls -la /tmp | grep snap | wc -l
$ cat << EOF > /tmp/note
> heredoc into a file
> EOF
$ export FOO=bar
$ env | grep FOO
$ exit
```

Use `ctrl-C` to clear the current line and get a fresh prompt, and `ctrl-D` (on an empty line) to exit.

---

## Features

### Mandatory

- [x] Interactive **prompt** (`readline`) with persistent line editing
- [x] Command **history** (`add_history`)
- [x] Executable lookup via `PATH`, and relative / absolute paths
- [x] Single quotes `'…'` (no interpretation)
- [x] Double quotes `"…"` (interpret only `$`)
- [x] Redirections: `<`, `>`, `>>`, `<<` (heredoc — no history update required)
- [x] Pipelines `cmd1 | cmd2 | …`
- [x] Environment-variable expansion `$VAR`
- [x] Special variable `$?` (exit status of the last foreground pipeline)
- [x] Signals: `ctrl-C`, `ctrl-D`, `ctrl-\` behave like in `bash`
- [x] Built-ins: `echo -n`, `cd`, `pwd`, `export`, `unset`, `env`, `exit`
- [x] At most **one** global variable, holding only a signal number
- [x] No leaks in our own code (readline's internal leaks are accepted by the subject)

### Not implemented (out of scope for mandatory)

- `;` command separator
- `\` line continuation
- unclosed quotes
- wildcards (`*`), `&&`, `||`, subshells `(...)` — these are bonus.

---

## Project layout

```
minishell-42/
├── Makefile
├── includes/         # public headers
├── libft/            # our libft, built first by the Makefile
├── srcs/
│   ├── parsing/      # lexer, tokens, expander, parser   (malkilan)
│   ├── exec/         # env, builtins, fork/exec, pipes,
│   │                 # redirections, heredoc             (rabdalqa)
│   ├── signals/      # signal handlers (interactive / heredoc / child)
│   └── main.c
└── README.md
```

Work split between the two of us:

- **malkilan** — parsing pipeline: `readline` loop, signals, lexer, quote handling, expander, parser, heredoc input collection.
- **rabdalqa** — execution pipeline: environment, built-ins, `fork`/`execve`, `pipe`/`dup2`, redirection wiring, child process management, exit-status handling.

We meet at a small, stable interface: a list of commands the parser produces and the executor consumes.

---

## Resources

Classic references we used while building this:

- **Bash Reference Manual** — <https://www.gnu.org/software/bash/manual/bash.html>
- **POSIX Shell Command Language** — <https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html>
- **Advanced Programming in the UNIX Environment** (W. Richard Stevens, S. A. Rago) — chapters on processes, signals, and I/O.
- **GNU Readline Library** — <https://tiswww.case.edu/php/chet/readline/readline.html>
- **GNU History Library** — <https://tiswww.case.edu/php/chet/readline/history.html>
- man pages: `bash(1)`, `readline(3)`, `signal(7)`, `sigaction(2)`, `fork(2)`, `execve(2)`, `pipe(2)`, `dup2(2)`, `wait(2)`, `waitpid(2)`, `access(2)`, `getenv(3)`.
- When the subject is silent, we use **bash** as the reference for expected behavior.

### Use of AI

In line with the 42 AI Instructions chapter, we used AI tools (ChatGPT / Claude) **as a learning and review assistant only**, never as a code generator we don't understand. Concretely:

- **Planning & study** — clarifying concepts (process groups, terminal modes, heredoc semantics, the difference between `wait` / `waitpid` / `wait3` / `wait4`) and turning the subject into a week-by-week plan.
- **Design review** — discussing data-structure choices for tokens and commands, and pros/cons of two-pass vs. on-the-fly expansion.
- **Debugging** — explaining unfamiliar error messages, reading `strace` / `ltrace` output, and proposing test cases (especially edge cases around quotes, empty expansions, and pipe error propagation).
- **Documentation** — drafting this `README` and Makefile rules, which we then reviewed and edited by hand.

All code in this repository was written, understood, and is defendable by us. AI output was always checked against `bash`, the subject, and peer review before being kept.

---

## Authors

- **malkilan** — Mustafa Alkilani
- **rabdalqa** — Roa'A Abdalqader

42 Amman · cursus · subject *Minishell — As beautiful as a shell* (v10.0).
