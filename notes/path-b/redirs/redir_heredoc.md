# redir_heredoc.c

Collects every here-document on the command line **before** anything forks,
buffering each body into a pipe whose read end is stored in the corresponding
`t_redir`. It also owns the special SIGINT behaviour that lets ctrl-C abandon a
half-typed heredoc and return to the prompt with status 130. Using a pipe rather
than a temporary file means there is nothing on disk to clean up and no `/tmp`
race to worry about.

## Walkthrough

### `static int delim_matches(const char *line, const char *delim)`

```c
return (!ft_strncmp(line, delim, ft_strlen(delim) + 1));
```
The `+ 1` is the whole trick: comparing `strlen(delim) + 1` bytes includes the
delimiter's terminating `\0`, so the comparison only succeeds when `line` also
ends at that point. Without it, `ft_strncmp(line, "EOF", 3)` would accept
`EOFX` and end the heredoc a line early. The delimiter must match the **entire**
line, so `EOF` ends it but `EOFX`, ` EOF` and `EOF ` do not — which is bash's
rule.

Note the line handed in has no trailing newline: `readline` strips it, and
`read_plain_line` in `srcs/input.c` stops before it and never appends it. So the
`\0` in the buffer really is where the user's line ended.

### `static char *heredoc_readline(void)`

```c
return (read_input_line("> "));
```
A one-line wrapper, but it names the continuation prompt in one place.
`read_input_line` (srcs/input.c) branches on `isatty`: on a terminal it calls
`readline("> ")`, so the user gets the familiar `>` continuation prompt with
line editing; when stdin is a pipe it falls back to a byte-at-a-time reader that
prints no prompt and does not echo. That echo suppression matters because
`readline` on a non-tty writes back everything it reads, which would duplicate
the heredoc body into any output a differential tester diffs against bash.

The byte-at-a-time reading is also why heredocs work at all when the shell is
scripted: a buffered read would swallow input past the delimiter that a later
command still needs on stdin.

### `static void write_heredoc_line(int fd, char *line, t_redir *redir, t_shell *sh)`

```c
if (redir->quoted_delim)
{
    ft_putendl_fd(line, fd);
    return ;
}
```
`quoted_delim` was set by the parser from the delimiter token's `had_quotes`
flag. Bash's rule: if **any** part of the delimiter was quoted — `<<"EOF"`,
`<<'EOF'`, even `<<E"OF"` — the body is taken completely literally, with no
variable expansion at all. So we write the raw line and add the newline that
`readline` stripped.

```c
quotes = ft_calloc(ft_strlen(line) + 1, 1);
if (!quotes)
    return ;
```
The expander's interface is `expand_word(value, quotes, sh)`, where `quotes` is
a parallel per-character array saying which quote state each character was in.
Heredoc text never went through the lexer, so there is no such array — and
semantically there should not be one, because a heredoc body has no quoting: a
`"` inside it is just a character. A calloc'd array is all zeros, and
`Q_NONE == 0`, so every character is marked unquoted. That makes `$VAR` expand
while leaving literal quote characters untouched (the expander only copies
literal runs verbatim; it does no quote removal of its own).

On allocation failure the line is silently dropped. That is a real, if
unreachable-in-practice, silent data loss path — it is worth naming as such
rather than dressing up as a design choice.

```c
out = expand_word(line, quotes, sh);
free(quotes);
if (out)
    ft_putendl_fd(out, fd);
free(out);
```
`expand_word` is path-a's expander, shared across the boundary precisely so the
heredoc body follows the same `$VAR` and `$?` rules as a command word. It
deliberately throws away the split mask, because a heredoc body is **never**
word-split — `<<EOF` containing `$SPACED` produces one line with spaces in it,
not several fields.

`quotes` is freed as soon as the expander is done with it, and `out` is freed
after writing. `free(NULL)` is a no-op, so the unconditional `free(out)` covers
the allocation-failure case too.

`expand_word` is deliberately not the same entry point the parser uses. Both go
through `expand_masked`, but `expand_word` passes `tilde = 0`, which makes
`tilde_prefix` return 0 without looking at the text. This matters because bash
performs parameter expansion inside a here-document body but never tilde
expansion: `<<EOF` containing the line `~/x` must print `~/x`, not `/home/u/x`.
An earlier version of this file shared the expander without the flag and got
that wrong; the flag is the fix.

`ft_putendl_fd` writes the string and a `\n` to `fd`, which is the pipe's write
end — so the body accumulates in the pipe buffer in order.

### `static int read_heredoc(t_redir *redir, t_shell *sh)`

```c
if (pipe(fds) < 0)
    return (-1);
```
The buffer for this heredoc. `fds[0]` is the read end that will become the
command's stdin, `fds[1]` the write end the loop below fills. Creating it before
installing the heredoc signal handler matters: the handler closes fd 0, and both
pipe fds are allocated above that, so they cannot be the ones closed.

A pipe holds 64 KB by default. A heredoc body larger than that would block the
`write` inside `ft_putendl_fd` forever, because nothing is reading yet — the
reader is a child that has not been forked. That is a genuine limitation of the
pipe-instead-of-tempfile approach; bash uses a temp file precisely to avoid it.
Nobody types 64 KB into a heredoc interactively, but a scripted test could.

```c
g_signal = 0;
signals_setup_heredoc();
```
The flag is cleared **before** the handler is installed, so a stale SIGINT from
a previous prompt cannot make this heredoc look interrupted. `signals_setup_heredoc`
installs `handle_heredoc_sigint` with no `SA_RESTART`, and sets SIGQUIT to
`SIG_IGN`.

```c
line = heredoc_readline();
while (line && !delim_matches(line, redir->filename))
{
    write_heredoc_line(fds[1], line, redir, sh);
    free(line);
    line = heredoc_readline();
}
free(line);
```
`redir->filename` holds the **delimiter** for a heredoc, not a filename — the
struct reuses the field, which the header comments on. The loop ends on one of
two conditions: the delimiter matched (`line` is non-NULL and equal), or `line`
is `NULL`.

`NULL` means the reader gave up, which happens on ctrl-D (real end of input) or
after ctrl-C, because the handler closed fd 0 and the next read fails. Both exit
the loop; the `g_signal` check below is what distinguishes them.

Every line is freed immediately after use, and the terminating `line` — which is
either the delimiter string or `NULL` — is freed after the loop. `free(NULL)` is
a no-op, so one call covers both.

Note that the ctrl-D case prints no warning. Bash emits
`warning: here-document delimited by end-of-file`. Not printing it is a silent
divergence, not a bug.

```c
close(fds[1]);
```
The write end is closed **here, in the parent, before any fork**. This is the
single most important close in the file. The read end is going to be handed to a
child as its stdin; that child sees EOF only when every write-end reference is
gone. If this close were omitted, the shell would still hold a write end, the
child would inherit it too, and `cat << EOF` would print the body and then hang
forever waiting for more input. Closing it before forking also guarantees no
child can accidentally inherit a write end, which is why this ordering is safer
than closing it later.

```c
if (g_signal == SIGINT)
{
    close(fds[0]);
    return (-1);
}
```
The interrupted case. The read end is closed so the buffered body is discarded
and no descriptor leaks — this heredoc's fd is never stored in the `t_redir`, so
nothing else would ever close it. Returning -1 tells `collect_heredocs` to
abandon the whole line, which ultimately makes `exec_line` return 130.

`g_signal` is read here, in normal code, rather than being acted on inside the
handler. That is the whole point of the one-global design: the handler records a
number, the main flow interprets it.

```c
redir->heredoc_fd = fds[0];
return (0);
```
Ownership of the read end passes to the `t_redir`. From here it is closed either
by `apply_one` (which dups it onto stdin, closes it and resets the field to -1)
or, if the command never runs, by `free_redirs` during `free_cmds`. Every path
has an owner, which is what keeps heredoc fds from leaking on syntax-error and
interrupted lines.

### `int collect_heredocs(t_cmd *cmds, t_shell *sh)`

```c
saved_stdin = dup(STDIN_FILENO);
failed = 0;
```
fd 0 is saved because `handle_heredoc_sigint` **closes it** to break the blocked
read. Without this backup, one ctrl-C during a heredoc would leave the shell with
no stdin at all and the next `readline` would return `NULL`, which the main loop
reads as ctrl-D and exits. Saving and restoring is what makes the shell survive
an interrupted heredoc.

The `dup` return is not checked; a -1 would make the `dup2` below a silent no-op.
Only reachable at the fd limit.

```c
while (cmds && !failed)
{
    redir = cmds->redirs;
    while (redir && !failed)
    {
        if (redir->type == R_HEREDOC && read_heredoc(redir, sh) < 0)
            failed = 1;
        redir = redir->next;
    }
    cmds = cmds->next;
}
```
Two nested walks: every command in the pipeline, every redirection in each
command, in source order. Order is observable — `cat << A << B` reads A's body
first and then B's, and only B ends up as stdin, so the prompts must appear in
that sequence.

The `failed` flag is used instead of an early `return` so that control always
reaches the stdin restore below. Returning directly from inside the loops would
skip the `dup2`/`close` pair and strand the shell without stdin — precisely the
bug the backup exists to prevent. Note the flag also stops the loops
immediately, so once one heredoc is interrupted the remaining ones are never
prompted for, matching bash's "abandon the whole line" behaviour.

```c
dup2(saved_stdin, STDIN_FILENO);
close(saved_stdin);
```
Restore, then release the backup. `dup2` re-establishes fd 0 whether or not the
handler closed it — dup'ing onto a closed fd number is perfectly legal and is
exactly how fd 0 comes back. The `close` prevents one leaked descriptor per
command line containing a heredoc; without it, a session that runs a few
thousand heredocs would exhaust the fd table.

Both lines run unconditionally, on the success path and the failure path.

```c
if (failed)
    return (-1);
return (0);
```
The caller (`exec_line`) turns -1 into `EXIT_SIG_BASE + SIGINT`, i.e. **130**,
and runs nothing. Any heredocs that were successfully collected before the
interruption still have live fds in their `t_redir` nodes; those are closed by
`free_redirs` when `process_line` calls `free_cmds`, so nothing leaks.

## Sequence

What happens, in order, for `cat << A << B` typed at an interactive prompt, with
the user entering `one`, `A`, `two`, `B`.

1. path-a has produced one `t_cmd` with `argv = {"cat"}` and two `t_redir` nodes
   in source order: R1 with `filename = "A"`, R2 with `filename = "B"`, both
   `type = R_HEREDOC`, both `heredoc_fd = -1` (set by `redir_new`), both
   `quoted_delim = 0` because neither delimiter was quoted.
2. `exec_line` calls `collect_heredocs(cmds, sh)`.
3. `saved_stdin = dup(0)` — say it returns 3. fd 3 is now a second reference to
   the terminal. `failed = 0`.
4. Outer loop enters the single command; inner loop reaches R1, which is a
   heredoc, so `read_heredoc(R1, sh)` is called.
5. `pipe(fds)` gives, say, `fds[0] = 4` (read) and `fds[1] = 5` (write). The
   shell now holds fds 0, 1, 2, 3, 4, 5.
6. `g_signal = 0`; `signals_setup_heredoc()` installs `handle_heredoc_sigint`
   for SIGINT with `sa_flags = 0`, and `SIG_IGN` for SIGQUIT.
7. `heredoc_readline()` prints `> ` and blocks. The user types `one`.
8. `delim_matches("one", "A")` compares 2 bytes (`"A"` plus its `\0`) and fails,
   so the loop body runs: `write_heredoc_line(5, "one", R1, sh)`. `quoted_delim`
   is 0, so a 4-byte zeroed `quotes` array is allocated, `expand_word` returns a
   copy of `one` (no `$` present), and `ft_putendl_fd` writes `one\n` into fd 5.
   `quotes` and `out` are freed; `line` is freed.
9. `heredoc_readline()` prompts again; the user types `A`. `delim_matches`
   succeeds, so the loop exits with `line == "A"`, which is then freed.
10. `close(5)` — the write end is gone. The pipe now holds `one\n` and has no
    writers, so any reader will get the data then EOF.
11. `g_signal` is still 0, so `R1->heredoc_fd = 4` and `read_heredoc` returns 0.
    The shell holds fds 0, 1, 2, 3, 4.
12. Inner loop advances to R2 and calls `read_heredoc(R2, sh)`.
13. `pipe(fds)` gives `fds[0] = 5` and `fds[1] = 6` (5 was just freed, so it is
    the lowest available). `g_signal` is reset to 0 and the heredoc handler is
    reinstalled — harmlessly redundant, but it keeps `read_heredoc`
    self-contained.
14. The user types `two`, which is written into fd 6, then `B`, which matches
    and ends the loop.
15. `close(6)`. `g_signal` is 0, so `R2->heredoc_fd = 5`, return 0.
16. Both loops end. `dup2(3, 0)` restores stdin — a no-op in this uninterrupted
    run, since fd 0 was never closed, but it is unconditional. `close(3)`
    releases the backup. `failed` is 0, so `collect_heredocs` returns 0.
17. `exec_line` continues. `cat` is not a builtin, so `run_pipeline` runs:
    `signals_ignore()`, no pipe (there is no `next`), `fork()`.
18. The child calls `pipeline_child`: `prev_read` is -1 and `cmd->next` is NULL,
    so no pipe work at all. It calls `child_exec`, which restores `SIG_DFL` and
    calls `apply_redirs`.
19. `apply_redirs` walks R1 then R2. For R1: `dup2(4, 0)` puts the first
    heredoc on stdin, `close(4)`, `R1->heredoc_fd = -1`. For R2: `dup2(5, 0)` —
    which implicitly closes the fd 0 that was just set — `close(5)`,
    `R2->heredoc_fd = -1`. **Only the last heredoc is actually readable**, which
    is bash's behaviour: the earlier ones are still consumed from the terminal
    but their content is discarded.
20. `execve("/bin/cat", {"cat"}, envp)`. `cat` reads `two\n` from stdin, hits
    EOF because the write end was closed back in step 15, prints it and exits 0.
21. The parent's `wait_for_all` reaps the child and returns 0. Note the parent
    still holds its own copies of fds 4 and 5 throughout — the child's `dup2`
    and `close` only affected the child's table.
22. `free_cmds` in `process_line` calls `free_redirs`, which finds both
    `heredoc_fd` values at -1 in the *parent's* copy... except the parent never
    ran `apply_one`, so in the parent they are still 4 and 5. `free_redirs`
    closes both. That is the close that finally releases the parent's copies,
    and it is why `free_redirs` has a `heredoc_fd >= 0` guard at all.

If instead the user had pressed **ctrl-C** while typing the second body: the
handler would set `g_signal = SIGINT`, write a newline, and `close(0)`. The
blocked read returns, `heredoc_readline` gives `NULL`, the loop exits,
`close(fds[1])` runs, `g_signal == SIGINT` so `close(fds[0])` discards the second
buffer and returns -1. `collect_heredocs` sets `failed`, skips any remaining
redirs, restores fd 0 from fd 3, closes fd 3, and returns -1. `exec_line` returns
130 without forking anything. R1's fd 4 is still open and is closed by
`free_redirs` when `process_line` frees the command list.

## Things to be ready to explain

- **Why collect heredocs before forking?** They read from the terminal, and only
  one process can meaningfully own the terminal at a time — several children
  prompting at once would interleave. It also puts ctrl-C handling in the
  interactive parent, where abandoning the line and returning 130 is natural.
- **Why a pipe instead of a temp file?** Nothing to create, name, or unlink, so
  there is no `/tmp` collision, no cleanup on crash, and no leftover file if the
  user interrupts. The cost is the 64 KB pipe buffer: a body larger than that
  would block the write, since the reader has not been forked yet.
- **Which close prevents `cat << EOF` from hanging?** `close(fds[1])` in
  `read_heredoc`, done in the parent before any fork. The child sees EOF only
  when every write-end reference is closed, and doing it before the fork
  guarantees no child inherits one.
- **Why does the handler close stdin instead of setting a flag only?** A flag
  alone would not wake a `read` that is already blocked in `readline`. Closing
  fd 0 makes the read fail, `readline` return `NULL`, and the loop exit. `close`
  is also async-signal-safe, which `printf` and `malloc` are not.
- **Why save and restore stdin in `collect_heredocs`?** Because the handler
  destroys fd 0. Without the `dup`/`dup2` pair, the shell would come back from
  one ctrl-C with no stdin and the next `readline` would return `NULL`, which
  the main loop reads as ctrl-D and exits.
- **Why the `failed` flag instead of returning early?** So the stdin restore at
  the bottom always runs. An early `return` from inside the loops would skip it
  and leave the shell unusable — the exact failure the backup exists to prevent.
- **Why is the `quotes` array all zeros?** Heredoc text never passed through the
  lexer and has no quote state; `Q_NONE` is 0, so a calloc'd array tells the
  expander every character is unquoted. That expands `$VAR` while leaving
  literal quote characters in place, which is bash's heredoc behaviour.
- **Known divergences?** No `warning: here-document delimited by end-of-file` on
  ctrl-D. A leading `~` in a body used to be tilde-expanded, because the shared
  word expander starts with `tilde_prefix`; that is fixed — `expand_word` now
  passes `tilde = 0`.
