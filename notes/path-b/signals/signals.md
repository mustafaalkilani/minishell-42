# signals.c

Home of the one global variable the subject permits, and of the two
parent-side signal dispositions: **interactive** (waiting at the prompt) and
**heredoc** (reading a here-document body). The third disposition — child — lives
in `signals_child.c`. Everything about why a handler may touch only a
`volatile sig_atomic_t`, and why it may not call `printf` or `malloc`, is
decided here.

## Walkthrough

### `volatile sig_atomic_t g_signal = 0;`

The single global. Three properties of the declaration, each of which an
evaluator will ask about:

- **`sig_atomic_t`** is the only integer type the C standard guarantees can be
  read and written in one uninterruptible step. A plain `int` is atomic on x86
  in practice, but the standard makes no such promise: on a platform where an
  `int` store is two instructions, a signal arriving between them would leave a
  half-written value. `sig_atomic_t` is defined precisely so that a handler can
  assign to it safely.
- **`volatile`** tells the compiler the variable can change behind its back.
  Without it, the optimiser is entitled to hoist `g_signal` into a register — the
  loop in `read_heredoc` never assigns to it, so the compiler could legitimately
  read it once and reuse the value forever, and the `if (g_signal == SIGINT)`
  check after the loop would test a stale copy. `volatile` forces a real memory
  load at every reference.
- **It holds a number and nothing else.** The subject allows one global and
  forbids it from carrying shell state — no pointer to `t_shell`, no struct, no
  buffer. That is why the handler records `sig` and the *normal* code
  (`shell_loop`, `read_heredoc`) converts it into a status or a control decision.

The rule about what a handler may call is the other half of this. Only
async-signal-safe functions are legal, because a signal can arrive at any
instruction, including in the middle of `malloc` while its internal free lists
are inconsistent. Calling `malloc` again from the handler at that moment
corrupts the heap. `printf` is unsafe for the same reason — it locks and
manipulates a `FILE` buffer, and it may `malloc`. `write` is on POSIX's
async-signal-safe list, which is why every handler here uses it.

### `static void handle_sigint(int sig)`

The interactive ctrl-C handler. Bash's behaviour is: abandon whatever is typed,
move to a new line, redraw an empty prompt, and leave the shell running.

```c
g_signal = sig;
```
Record the number. `shell_loop` reads it after `readline` returns and sets
`sh->last_status = 128 + SIGINT` = 130, then clears the flag. The translation
cannot happen here, because assigning to `sh->last_status` would mean the handler
touching shell state, which the one-global rule forbids.

```c
write(STDOUT_FILENO, "\n", 1);
```
The terminal has echoed `^C` but no newline, so the redrawn prompt would sit on
the same line. `write` rather than `printf` because it is async-signal-safe and
unbuffered — a `printf` here could interleave badly with readline's own output
or deadlock on the stdio lock if the interrupted code held it.

```c
rl_replace_line("", 0);
rl_on_new_line();
rl_redisplay();
```
The readline dance. `rl_replace_line("", 0)` empties the internal edit buffer so
the half-typed command is discarded (the second argument, `clear_undo`, is 0, so
the undo list is left alone). `rl_on_new_line` tells readline the cursor is now
at the start of a fresh line — otherwise it would try to erase text at a
position that no longer holds it. `rl_redisplay` repaints the prompt with the
now-empty buffer.

**Be honest about this: none of these three is async-signal-safe.** `rl_redisplay`
in particular can allocate. This is the one deliberate signal-safety violation in
the project, and it is the standard 42 minishell solution because readline
exposes no safe way to do it — the alternatives are `rl_done = 1` with an event
hook, or moving the redisplay out of the handler entirely. In practice the race
window is tiny and the shell is idle inside `readline` when SIGINT arrives, so it
does not misbehave; but if an evaluator asks "is your handler async-signal-safe",
the correct answer is "the `write` is, the three `rl_*` calls are not, and here
is why we accept that".

A second subtlety worth knowing: readline installs its own SIGINT handler while
`readline()` is running (`rl_catch_signals` defaults to 1 and nothing in this
project changes it). Readline's handler cleans up its terminal state and then
re-raises the signal so the previously installed handler — ours — runs. The net
effect is the behaviour described above. This is the accepted explanation for
why the pattern works, but the exact interleaving is a readline implementation
detail rather than something the code controls, so present it as such.

### `static void handle_heredoc_sigint(int sig)`

```c
g_signal = sig;
write(STDOUT_FILENO, "\n", 1);
close(STDIN_FILENO);
```
A different problem entirely. During a heredoc the shell is blocked reading a
line; there is no command being edited to erase and no prompt to repaint in the
readline sense. Setting a flag alone would not help, because the process is
already blocked inside a `read` and would simply resume waiting once the handler
returned.

`close(STDIN_FILENO)` is what breaks the block: the pending read fails, and
every subsequent read on fd 0 fails too, so `read_input_line` returns `NULL`,
the collection loop in `read_heredoc` exits, and the `g_signal == SIGINT` check
afterwards distinguishes this from a genuine ctrl-D. Destroying fd 0 is drastic,
which is exactly why `collect_heredocs` `dup`s it beforehand and `dup2`s it back
afterwards — that backup exists solely to undo this line.

Both calls here **are** async-signal-safe: `write` and `close` are both on
POSIX's list. So this handler, unlike the interactive one, is fully correct by
the book.

### `void signals_setup_interactive(void)`

```c
struct sigaction act;

ft_bzero(&act, sizeof(act));
```
`sigaction` is used rather than `signal` because `signal`'s semantics are
historically unportable — on some systems it resets the disposition to default
after one delivery, and it gives no control over the mask or flags. Zeroing the
whole struct first means no uninitialised field is ever passed to the kernel,
which matters because `struct sigaction` has padding and implementation-specific
members (`sa_restorer` on Linux) that are not set explicitly.

```c
act.sa_handler = handle_sigint;
sigemptyset(&act.sa_mask);
act.sa_flags = SA_RESTART;
sigaction(SIGINT, &act, NULL);
```
`sa_mask` empty means no *additional* signals are blocked while the handler
runs. SIGINT itself is still blocked automatically during its own handler unless
`SA_NODEFER` is set, so the handler cannot re-enter itself.

`SA_RESTART` asks the kernel to restart an interrupted slow syscall instead of
failing it with `EINTR`. Here that means the `read` inside `readline` resumes
after the handler returns, so the user is left sitting at a freshly drawn empty
prompt on the *same* `readline` call rather than having the call return an empty
line. The practical consequence chains through `shell_loop`: `g_signal` stays set
until the user actually submits a line, at which point the loop converts it to
status 130 *before* `process_line` runs — which is why `^C` followed by typing
`echo $?` prints 130.

```c
act.sa_handler = SIG_IGN;
sigaction(SIGQUIT, &act, NULL);
```
The `act` struct is reused, so SIGQUIT inherits `SA_RESTART` and the empty mask;
neither means anything for `SIG_IGN`, since no handler runs. Ctrl-backslash at an
interactive bash prompt does nothing at all, so it is ignored rather than
handled. Ignoring rather than installing a do-nothing handler is also what keeps
it from generating `EINTR` anywhere.

### `void signals_setup_heredoc(void)`

Identical in shape, with two deliberate differences.

```c
act.sa_handler = handle_heredoc_sigint;
sigemptyset(&act.sa_mask);
act.sa_flags = 0;
sigaction(SIGINT, &act, NULL);
```
`sa_flags = 0` — **no** `SA_RESTART`, which is the opposite of the interactive
case and is the point of having a separate setup function. Here we *want* the
blocked read to fail rather than resume: the handler has just closed fd 0
precisely so the read cannot succeed, and restarting it would only produce the
same failure a moment later. Without `SA_RESTART` the syscall returns `EINTR`
immediately, the reader gives up, and `read_heredoc` reaches its `g_signal`
check.

```c
act.sa_handler = SIG_IGN;
sigaction(SIGQUIT, &act, NULL);
```
Same as interactive: ctrl-backslash during a heredoc does nothing in bash.

Note this disposition is installed inside `read_heredoc`, once per heredoc, and
is never explicitly torn down. It is superseded by `signals_ignore()` when a
pipeline starts and by `signals_setup_interactive()` at the top of the next
`shell_loop` iteration, so it can never leak into the next prompt.

## Things to be ready to explain

- **Why exactly one global, and why that type?** The subject allows one, and it
  may only carry a signal number. `volatile` stops the compiler caching it in a
  register across the loops that poll it; `sig_atomic_t` is the only type the C
  standard guarantees is written in one uninterruptible step, so a signal cannot
  catch a half-written value.
- **Why can a handler not call `printf` or `malloc`?** A signal can arrive
  mid-`malloc`, while the allocator's internal structures are inconsistent;
  calling back into it corrupts the heap. `printf` is unsafe for the same reason
  and additionally takes a stdio lock the interrupted code may already hold. Only
  the POSIX async-signal-safe list is legal, which is why `write` and `close` are
  used.
- **Is your interactive handler actually async-signal-safe?** No, and it is the
  honest answer: `rl_replace_line`, `rl_on_new_line` and `rl_redisplay` are not on
  the safe list and `rl_redisplay` may allocate. It is the accepted 42 solution
  because readline offers no safe alternative, and the shell is idle inside
  `readline` when the signal arrives. The heredoc handler, by contrast, uses only
  `write` and `close` and is fully safe.
- **Why does the heredoc handler close stdin?** Because a flag alone cannot wake
  a read that is already blocked. Closing fd 0 makes it fail so the loop can
  exit. `collect_heredocs` `dup`s fd 0 beforehand and restores it afterwards,
  which is the only reason the shell survives it.
- **Why `SA_RESTART` for interactive but `sa_flags = 0` for heredoc?** At the
  prompt we want `readline` to keep going after the handler redraws — the user is
  still editing. In a heredoc we want the read to fail so the collection loop
  ends. Same signal, opposite requirement, hence two setup functions.
- **Why `sigaction` rather than `signal`?** `signal` has historically varying
  semantics (some systems reset the disposition after one delivery) and gives no
  control over `sa_mask` or `SA_RESTART`, which this file depends on.
- **Who converts `g_signal` into 130?** `shell_loop` in `srcs/main.c` for an
  interrupted prompt, and `exec_line` for an interrupted heredoc (via
  `collect_heredocs` returning -1). Never the handler, because that would mean
  writing to `t_shell` from signal context.
