# input.c

The single place where a line of input is obtained. It exists because
`readline` is the right tool on a terminal and the wrong tool everywhere else:
when stdin is not a tty, readline echoes what it reads, which would duplicate
every command into stdout and break any comparison against bash. This file
picks between readline and a minimal hand-rolled reader based on `isatty`.

## Walkthrough

### `static char *read_plain_line(void)`

The non-interactive reader. Its defining property is that it reads **one byte
at a time** and stops at the newline.

```c
line = ft_strdup("");
buf[1] = '\0';
```
Start from an owned empty string so that the join loop below has something
valid to free on its first pass, and so a line with no characters before EOF
is still distinguishable from a NULL return. `buf` is two bytes with the
second permanently `'\0'`, which turns the one-byte read into a valid
C string that `ft_strjoin` can consume without a temporary.

```c
n = read(STDIN_FILENO, buf, 1);
while (line && n == 1 && buf[0] != '\n')
{
    joined = ft_strjoin(line, buf);
    free(line);
    line = joined;
    n = read(STDIN_FILENO, buf, 1);
}
```
The loop condition carries three separate guards and each one matters:

- `line` — if `ft_strjoin` returned NULL because malloc failed, `line` becomes
  NULL and the loop exits instead of dereferencing it. The NULL then falls
  through to the caller as an EOF-like result rather than crashing.
- `n == 1` — anything else is EOF (`0`) or an error (`-1`), and both must end
  the line.
- `buf[0] != '\n'` — the newline terminates the line and is **consumed but not
  stored**, matching what `readline` gives us. That keeps the two code paths
  interchangeable for the caller: neither ever returns a trailing newline.

`joined`/`free(line)`/`line = joined` is the standard three-step to avoid
leaking the previous buffer. Yes, this is O(n²) in the length of the line
because every byte reallocates and copies the whole string. That is a
deliberate trade: input lines are short, and the alternative — a growable
buffer — is more code and more places to get the bookkeeping wrong under
norminette's 25-line function limit.

**Why one byte at a time, and not a 4096-byte buffer?** This is the question
to be ready for. `read()` on a shared file description consumes from the
kernel's offset. If we asked for 4096 bytes we would pull the *rest of the
script* into this process's memory, not just the current line. A forked child
inherits the same open file description, so when `cat << EOF` or any command
reading stdin runs, those bytes are already gone — the child sees EOF or the
wrong data. Reading exactly up to and including the newline leaves the file
offset precisely at the start of the next line, so heredoc bodies and
commands that read stdin still see everything they should. Interactive
readline has the same property because the terminal driver is line-buffered.

```c
if (line && n <= 0 && !*line)
{
    free(line);
    return (NULL);
}
return (line);
```
The EOF contract. NULL is returned only when we hit EOF **and** collected
nothing — a genuinely empty end of input, which the main loop treats as
ctrl-D and exits on. If the stream ended mid-line (a final line with no
trailing newline, e.g. `printf 'echo hi'`), `*line` is non-empty and we return
that content: the last line still runs, exactly as bash would. The next call
will hit EOF with an empty buffer and return NULL then. Getting this wrong in
either direction either drops the final command or loops forever at EOF.

`free(line)` before the NULL return releases the empty `ft_strdup("")`; the
caller only frees non-NULL results.

### `char *read_input_line(const char *prompt)`

```c
if (isatty(STDIN_FILENO))
    return (readline(prompt));
return (read_plain_line());
```
Two lines, one decision, and it is the reason the file exists.

On a terminal we want `readline`: it prints the prompt, gives line editing and
arrow-key history, and returns a malloc'd line without the newline. That is
what the subject asks for.

When stdin is a pipe or a file, `readline` is actively harmful. It still
believes it is driving a terminal, so it **echoes every character it reads
back to stdout**. Run `echo 'echo hi' | ./minishell` with readline and stdout
contains `minishell$ echo hi` followed by `hi`, instead of just `hi`. Since
`tests/edges.sh` diffs our stdout against bash's byte for byte, and every
public tester pipes scripts in, that echo would fail essentially every test.
The prompt string is also dropped on this path, for the same reason — bash
prints no prompt when it is not interactive.

The single `isatty` check therefore gates three related behaviours across the
project, all consistent with bash: the prompt, the use of readline, and (in
`main.c`) the `exit` message on ctrl-D. `tests/signals.py` covers both sides
of the branch: `test_prompt_on_tty` runs the shell on a real pty and asserts a
prompt appears, while `test_no_prompt_when_piped` pipes `echo hi` in and
asserts stdout is exactly `hi\n`.

Both branches return a heap string that the caller owns and frees, so the
caller never has to know which one ran.

## Things to be ready to explain

- **Why not use `readline` for everything?** Because it echoes its input when
  stdin is not a terminal. Every piped test would then see the command text
  duplicated on stdout and diverge from bash.
- **Why read one byte at a time?** A larger `read` would pull bytes past the
  newline into this process. A forked child shares the same file offset, so
  `cat << EOF` or any command reading stdin would find those bytes missing.
  Stopping at the newline leaves the offset exactly where the child needs it.
- **Isn't byte-at-a-time joining O(n²)?** Yes, and it is accepted: shell lines
  are short, and a growable buffer would cost more code under norminette's
  function-length limit than the performance is worth here.
- **When does this return NULL?** Only at EOF with nothing collected. A final
  line without a trailing newline is still returned and still executed; the
  *next* call returns NULL.
- **Where is the newline?** Consumed and discarded, so this function matches
  `readline`'s contract exactly and the caller cannot tell the two paths
  apart.
- **What else does the same `isatty` check control?** The prompt, and the
  `exit` message printed on ctrl-D in `shell_loop`. All three must agree or
  piped output diverges from bash.
