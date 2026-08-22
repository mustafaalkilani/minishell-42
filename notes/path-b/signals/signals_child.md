# signals_child.c

The other two signal dispositions, both used around forking. `signals_ignore`
makes the *parent* deaf to ctrl-C and ctrl-backslash while a foreground pipeline
runs, and `signals_setup_child` puts the defaults back inside each forked child
so that ctrl-C actually kills it. Together with `signals.c` these are the three
states the shell moves between: interactive, heredoc, and running-a-command.

## Walkthrough

### `void signals_setup_child(void)`

Called as the very first statement of `child_exec`, in the child, after `fork`
and before any redirection or `execve`.

```c
struct sigaction act;

ft_bzero(&act, sizeof(act));
act.sa_handler = SIG_DFL;
sigemptyset(&act.sa_mask);
act.sa_flags = 0;
sigaction(SIGINT, &act, NULL);
sigaction(SIGQUIT, &act, NULL);
```
`SIG_DFL` is the kernel's default action: terminate for SIGINT, terminate and
dump core for SIGQUIT. Restoring it is what makes ctrl-C during `cat` kill the
`cat` rather than being swallowed.

The reason this call is **necessary** is the single most important fact in the
file, and it is a two-part argument:

1. `fork` copies the parent's signal dispositions. The parent called
   `signals_ignore()` just before the loop in `run_pipeline`, so every child
   starts life with SIGINT and SIGQUIT set to `SIG_IGN`.
2. `execve` resets *installed handlers* to `SIG_DFL` — a handler function cannot
   survive, because the code it points at no longer exists — but it explicitly
   **preserves `SIG_IGN`**. So an ignored signal stays ignored across the exec.

Put together: without this call, `cat` with no arguments would be launched with
SIGINT ignored and would be completely unkillable by ctrl-C. The shell would
appear frozen. This is exactly why the reset cannot be left to `execve` to do
implicitly, and why it is a distinct function rather than being folded into one
of the setups in `signals.c`.

It is placed before `apply_redirs` so that a child which blocks — opening a FIFO
with no writer, for example — is already interruptible while it blocks.

The knock-on effect is the exit code. A child killed by SIGINT is reported by
`waitpid` with `WIFSIGNALED` true and `WTERMSIG` equal to 2, and
`status_to_exit` turns that into `128 + 2 = 130`. Ctrl-backslash gives
`128 + 3 = 131`. Neither number would ever appear if the disposition were left
ignored, because the child would simply never die.

`sa_flags = 0` and an empty `sa_mask` are both irrelevant for `SIG_DFL` — no
handler runs, so there is nothing to mask and no syscall-restart policy to
choose — but setting them keeps the struct fully initialised and the three setup
functions structurally identical.

Only SIGINT and SIGQUIT are touched. Everything else was already at its default
in the parent and is inherited unchanged, so there is nothing to undo.

### `void signals_ignore(void)`

Called once in the parent, at the top of `run_pipeline`, before the fork loop.

```c
act.sa_handler = SIG_IGN;
sigemptyset(&act.sa_mask);
act.sa_flags = 0;
sigaction(SIGINT, &act, NULL);
sigaction(SIGQUIT, &act, NULL);
```
While a foreground command runs, ctrl-C belongs to the child, not to the shell.
The terminal driver sends SIGINT to the entire foreground **process group**, and
this project does not call `setpgid`, so the children are in the shell's own
group and receive the same signal the shell does. If the shell kept the
interactive handler installed, ctrl-C during `sleep 10` would both kill the
`sleep` and make the shell print a newline and redraw a prompt on top of the
`wait_for_all` that has not returned yet. Setting `SIG_IGN` makes the shell
simply not react; it learns about the interrupt afterwards, by decoding the
child's wait status in `report_signal`.

Three further consequences of this call, each worth being able to state:

- **It is what the children then have to undo.** `signals_setup_child` exists
  only because this call ran first.
- **It keeps `wait_for_all` correct.** An ignored signal does not interrupt a
  blocking syscall, so the `waitpid` loop cannot return -1 with `EINTR` and leave
  zombies behind. If this were an installed handler without `SA_RESTART`, the
  reaping loop would need an `EINTR` retry.
- **It is never undone here.** `run_pipeline` does not restore the interactive
  disposition when it returns. That is deliberate — `shell_loop` in
  `srcs/main.c` calls `signals_setup_interactive()` at the top of every
  iteration, so the prompt is always put back into the interactive state
  regardless of which path the previous line took. Doing it in one place rather
  than at every exit of `run_pipeline` is what keeps the state machine simple.

Note that `run_parent_builtin` does **not** call `signals_ignore`. A lone builtin
runs in the shell with the interactive handler still installed, so ctrl-C during
`cd` or `export` would run `handle_sigint` and redraw a prompt. Those builtins
are effectively instantaneous, so the window is negligible, but it is an
asymmetry with the pipeline path rather than a designed behaviour — say so if
asked, rather than inventing a rationale.

## Things to be ready to explain

- **Why must the child reset signals if `execve` resets handlers anyway?**
  Because `execve` resets *handlers* but deliberately **preserves `SIG_IGN`**.
  The parent set SIG_IGN before forking, the child inherited it, and it would
  survive the exec — leaving `cat` unkillable by ctrl-C. `SIG_DFL` has to be put
  back explicitly.
- **Why does the parent ignore SIGINT while a command runs?** The terminal sends
  the signal to the whole foreground process group, which includes the shell.
  Ignoring it means the child dies and the shell survives, which is bash's
  behaviour. The shell finds out about the interrupt afterwards from the child's
  wait status, not from a handler.
- **Where does 130 actually come from?** From this file indirectly: `SIG_DFL`
  lets the child die of SIGINT, `waitpid` reports `WIFSIGNALED` with
  `WTERMSIG == 2`, and `status_to_exit` computes `EXIT_SIG_BASE + 2`. Without
  the reset the child would never die and the number would never appear.
- **Who restores the interactive handlers after a pipeline?** `shell_loop`, at
  the top of its next iteration. `run_pipeline` intentionally leaves the parent
  in the ignoring state, so there is exactly one place that owns the restore.
- **Why is `signals_setup_child` called before `apply_redirs`?** So the child is
  interruptible even if a redirection blocks — opening a FIFO with no writer is
  the standard example.
- **Are these three setup functions not duplicated code?** They differ in the
  handler and in `sa_flags`, and those differences are exactly the semantics:
  `SA_RESTART` at the prompt, none in a heredoc, `SIG_DFL` in the child,
  `SIG_IGN` in the waiting parent. Merging them behind a parameter would hide
  the one thing each is for.
