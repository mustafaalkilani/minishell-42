# path_a.h

The private header for the front end: lexer, parser and expander. It declares
one type, `t_exp`, which exists only inside the expander, and the internal
prototypes that the three subsystems use to talk to each other. Nothing here
is visible to path-b — the public surface of this half is the five functions
declared in `minishell.h` (`lex`, `expand`, `parse`, `free_tokens`,
`free_cmds`) plus the shared `expand_word`. Splitting the header this way is
what let the two partners work in parallel without stepping on each other's
namespaces.

## Walkthrough

### Include guard and `# include "minishell.h"`

```c
#ifndef PATH_A_H
# define PATH_A_H

# include "minishell.h"
```
The guard follows norminette's rule of `#` in column 0 with the directive
indented after it. Including `minishell.h` rather than the system headers
directly means every path-a `.c` file needs exactly one `#include "../path_a.h"`
line and automatically gets `t_token`, `t_cmd`, `t_shell`, the quote macros and
libft. It also guarantees that the shared types are defined identically on both
sides of the project, since there is only one definition.

### `typedef struct s_exp`

```c
typedef struct s_exp
{
    char    *out;
    char    *mask;
    t_shell *sh;
}   t_exp;
```
The expander's accumulator. Every expansion in this half of the project builds
one of these, and understanding the `mask` field is understanding the whole
expander.

**`out`** is the expanded text built up incrementally. It starts as
`ft_strdup("")` in `expand_masked` so the append helper always has an owned
string to join onto, and it grows one chunk at a time as literal runs and
variable values are appended.

**`mask`** is the interesting field: a parallel byte array, one byte per
character of `out`, holding `'1'` when that character arrived through an
**unquoted** expansion and `'0'` otherwise. It is deliberately ASCII `'1'` and
`'0'` rather than numeric 1 and 0, because that makes it an ordinary
NUL-terminated C string. `ft_strjoin`, `ft_strlen` and `ft_substr` all work on
it unchanged, so `exp_append` can grow text and mask with the same two calls to
`join_free` instead of needing a separate length-tracked buffer.

Why the mask needs to exist at all: field splitting in bash applies **only** to
the results of unquoted expansions, never to text the user typed literally and
never to quoted expansions. So given `export S="a b c"`:

- `echo $S` must produce three arguments — the spaces came from an unquoted
  expansion, mask `'1'`, so they split.
- `echo "$S"` must produce one argument — same characters, but the `$` was
  inside double quotes, so `append_var` passes flag `'0'` and the spaces do not
  split.
- `echo "a b"` must produce one argument — literal text, always flag `'0'`.

After expansion the three cases are byte-identical in `out`. The mask is the
only thing that distinguishes them, which is why `is_split` in
`expander_split.c` tests `e->mask[i] == '1' && is_space(e->out[i])` — both
conditions, never just the whitespace.

The invariant that makes it work is that `out` and `mask` are always the same
length. `exp_append` enforces it by building a `pad_new(ft_strlen(chunk), flag)`
run of flag bytes for every chunk it appends, and by poisoning both to NULL
together if either allocation fails. That last detail is why the callers only
ever have to test `e.out` once at the end instead of checking after every
append — a very norminette-friendly property when functions are capped at 25
lines.

**`sh`** is the shell state, carried inside the struct rather than passed as a
fourth parameter to every helper. `var_lookup` needs `sh->env` for the
environment and `sh->last_status` for `$?`, and `tilde_prefix` needs `sh->env`
for `HOME`. Bundling it keeps parameter counts under norminette's limit of
four.

Note there is no length or capacity field. The struct is intentionally three
pointers: everything is a NUL-terminated string, so length is always
recoverable and there is no bookkeeping to get out of sync.

### Prototype group: lexer internals

```c
t_token         *token_new(t_token_type type, char *value, char *quotes);
void            token_add_back(t_token **head, t_token *tok);
int             word_measure(const char *s, size_t i, size_t *end);
void            word_fill(const char *s, size_t i, char *value, char *quotes);
t_token         *read_operator(const char *s, size_t *i);
```
Responsible for turning raw text into the `t_token` list, with quote
delimiters removed and per-character quote state recorded.

`token_new` and `token_add_back` are the list primitives. `token_new` **takes
ownership** of both `value` and `quotes` and frees them on every failure path,
which is why no caller in the lexer has cleanup branching. `quotes` may be NULL
for operator tokens, since an operator has no quote metadata to record.

`word_measure` and `word_fill` are the two halves of a deliberate two-pass
scan, and the pairing is worth defending. The first pass counts how many
characters survive once quote delimiters are dropped and reports where the word
ends; the second pass writes into a buffer of exactly that size. Two passes
means one `malloc` of the exact length instead of a grow-and-realloc loop —
simpler, and it keeps both functions inside the line limit. `word_measure`
returning `-1` is the unclosed-quote syntax error: the subject does not require
the multi-line continuation prompt bash gives, so an unterminated quote is
simply rejected.

`read_operator` handles `|`, `<`, `>`, `<<`, `>>`, testing the two-character
forms first so `>>` is never mis-read as two `>` tokens, and advancing the
caller's index by the operator's length through the `size_t *i` out-parameter.

### Prototype group: parser internals

```c
t_cmd           *cmd_new(void);
int             cmd_add_arg(t_cmd *cmd, char *value);
int             cmd_add_redir(t_cmd *cmd, t_token *op, t_token *target);
int             parse_redir_step(t_cmd *cmd, t_token **tokens);
void            free_redirs(t_redir *redirs);
int             syntax_error(const char *near);
```
Responsible for consuming the already-expanded token list and producing the
`t_cmd` pipeline the executor runs.

`cmd_new` allocates a command whose `argv` is already a one-element
NULL-terminated array, so an empty command is immediately valid for `execve`
and `cmd_add_arg` never has a special first-argument case.

`cmd_add_arg` grows `argv` by full reallocation on every call and takes
ownership of `value`. Quadratic in the argument count, and accepted for the
same reason as the byte-at-a-time reader in `input.c`: command lines have a
handful of arguments, and tracking a capacity field would cost more code than
the copies cost time.

`cmd_add_redir` and `parse_redir_step` split the redirection work in two.
`parse_redir_step` is the grammar rule — an operator must be followed by a
`T_WORD`, and anything else (including end of input, reported as `newline`)
is the classic bash syntax error. `cmd_add_redir` is the list-building half,
appending to the tail so redirections stay in source order, which is what makes
`> a > b` create both files but leave stdout on `b`.

`free_redirs` is separate from `free_cmds` because a redirection may own a live
file descriptor: it closes `heredoc_fd` when it is `>= 0` before freeing the
node. Forgetting that would leak fds on every heredoc that never got applied,
for instance when an earlier redirection in the same command failed.

`syntax_error` is declared here rather than in `minishell.h` because only the
parser reports syntax errors. It prints the message and returns the value the
caller propagates, so error handling stays a single-line `return
(syntax_error(...))` at each call site.

### Prototype group: expander internals

```c
char            *var_lookup(const char *name, t_shell *sh);
int             var_name_len(const char *s, const char *quotes);
char            *join_free(char *dst, char *src);
char            *pad_new(size_t n, char flag);
void            exp_append(t_exp *e, char *chunk, char flag);
int             split_token(t_token **tok, t_exp *e);
size_t          tilde_prefix(t_exp *e, const char *value, const char *quotes,
                    int enabled);
int             is_quote_prefix(const char *quotes, size_t i);
```
Responsible for substituting variables and then splitting the result into
fields, all while respecting the quote metadata the lexer recorded.

`var_lookup` resolves a name to a freshly allocated string. It handles `?`
itself rather than going through the environment list, because `$?` is shell
state and never appears in `env`. An unset variable returns `ft_strdup("")`
rather than NULL, so the caller has no special case — an unset variable
expanding to nothing is normal, not an error.

`var_name_len` is the name scanner, and the reason it takes the `quotes`
pointer as well as the text is `Q_BREAK`. A name is `[A-Za-z_][A-Za-z0-9_]*`,
but it must also stop at a stripped quote boundary. In `$T"o"` the `"` is gone
from `value`, so without the quote array the scanner would read the name as
`To`; the `Q_BREAK` bit on the `o` is what stops it at `T`. The same check at
index 0 makes `"$"USER` a literal `$` followed by `USER`, which is bash's
behaviour.

`join_free`, `pad_new` and `exp_append` are the three-function allocation core.
`join_free` concatenates and frees both inputs, returning NULL if either was
already NULL — the poison-propagation trick that lets a long chain of appends
be checked once at the end. `pad_new` produces a run of `n` identical flag
bytes so the mask can grow by exactly as much as the text. `exp_append` is the
only function that should ever touch `out` and `mask`, and it always touches
both, which is what preserves the equal-length invariant.

`tilde_prefix` and `is_quote_prefix` handle the two prefix forms, both returning
how much (if anything) to skip. `tilde_prefix` expands a leading `~` to `$HOME`
only when it is completely unquoted (`quotes[0] != Q_NONE` rejects both quoted
text and a `Q_BREAK` left by `''~`) and only when it stands alone or precedes a
`/`, which leaves `~user` untouched since other users' home directories are not
resolved. Its result is appended with flag `'0'` because bash does not
word-split a tilde expansion even when `HOME` contains spaces. The `enabled`
parameter is what separates the two callers: `expand` passes 1 for command
words, `expand_word` passes 0 for heredoc bodies, because bash expands
parameters inside a here-document but never a tilde. Putting the flag test
inside `tilde_prefix` rather than at the call site keeps `expand_masked` under
the 25-line limit.
`is_quote_prefix` detects an unquoted `$` sitting immediately before an opening
quote — bash's `$"..."` and `$'...'` forms. With no locale catalogue and no
escape decoding to do, the `$` is simply dropped and the quoted text stands,
which is what `echo $"HOME"` printing `HOME` in `tests/edges.sh` checks.

`split_token` is where field splitting actually happens, and its signature is
the giveaway: it takes `t_token **`, not `t_token *`. One word can expand into
several arguments, so extra fields are spliced into the token list immediately
after the token being expanded, and the double pointer lets it advance the
caller's cursor to the last field it produced — so `expand`'s outer loop
continues from the right place and does not try to re-expand the fields it just
created. If the expansion yields no fields at all, the token is set to the empty
string rather than removed, leaving the parser to consult `had_quotes` to decide
between dropping it (`$NOPE`) and keeping an empty argument (`"$NOPE"`).

## Things to be ready to explain

- **What is `mask` for, and why ASCII `'1'`/`'0'`?** It marks which characters
  came from an unquoted expansion, which is the only thing that distinguishes
  `$S` (splits into fields) from `"$S"` (does not) after both have produced
  identical text. ASCII digits make it a plain C string, so the same
  `ft_strjoin`/`ft_strlen`/`ft_substr` helpers work on it as on `out`.
- **How do `out` and `mask` stay the same length?** Only `exp_append` writes
  them, and it always appends a `pad_new` run of exactly `ft_strlen(chunk)`
  flag bytes alongside every chunk. If either allocation fails, `join_free`
  poisons both to NULL, so one check at the end covers the whole chain.
- **Why does `var_name_len` need the quotes array?** To stop the name at a
  stripped quote boundary. `$T"o"` and `$To` are identical in `value` once
  quotes are removed; the `Q_BREAK` bit is the only remaining evidence, and
  without it the wrong variable name would be looked up.
- **Why does `split_token` take a `t_token **`?** Because one word can become
  several tokens. It splices the extra fields into the list after the current
  token and moves the caller's cursor onto the last one, so `expand` resumes
  past them instead of re-expanding them.
- **Why two passes (`word_measure` then `word_fill`) instead of one?** So the
  buffer is malloc'd once at exactly the right size after quote removal, with
  no grow-and-realloc loop. It also keeps each function short enough for
  norminette.
- **Why does `t_exp` carry `sh`?** `var_lookup` needs `env` and `last_status`,
  `tilde_prefix` needs `HOME`. Bundling it into the accumulator keeps helper
  parameter counts within norminette's limit of four.
- **Why is `syntax_error` in this header and not `minishell.h`?** Only the
  parser detects syntax errors. Keeping it private documents that, and stops
  path-b from ever emitting a parser-shaped message.
