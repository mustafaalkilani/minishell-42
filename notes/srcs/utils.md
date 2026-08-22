# utils.c

Four tiny helpers that both halves of the project share: the error printer
that gives every diagnostic the same shape, two character classifiers used by
the lexer, and the blank-line test used by the main loop. They live in `srcs/`
rather than in `path-a/` or `path-b/` precisely because both partners call
them, so neither half owns them.

## Walkthrough

### `void shell_error(const char *ctx, const char *arg, const char *msg)`

```c
ft_putstr_fd("minishell: ", STDERR_FILENO);
```
Every message starts with the shell's name, the way bash starts every message
with `bash: `. Hard-coding the prefix in one function means there is exactly
one place to change it and no risk of a stray message going out without it.
Writing to `STDERR_FILENO` matters more than it looks: diagnostics must never
land on stdout, or a pipeline like `no_such_cmd | wc -l` would count the error
message as data, and `tests/edges.sh` compares stdout byte for byte.

```c
if (ctx)
{
    ft_putstr_fd((char *)ctx, STDERR_FILENO);
    ft_putstr_fd(": ", STDERR_FILENO);
}
if (arg)
{
    ft_putstr_fd((char *)arg, STDERR_FILENO);
    ft_putstr_fd(": ", STDERR_FILENO);
}
```
Both middle fields are optional, and that is the whole point of the design.
Bash's error format is a variable number of colon-separated context fields
followed by the message. With three parameters and two null checks, one
function covers all the shapes we need:

- `shell_error("cd", "/nope", "No such file or directory")` gives
  `minishell: cd: /nope: No such file or directory` — a builtin failing on a
  specific argument.
- `shell_error("no_such_cmd", NULL, "command not found")` gives
  `minishell: no_such_cmd: command not found`.
- `shell_error(NULL, NULL, "syntax error near unexpected token `|'")` gives
  the short form with no context at all.

The casts to `char *` exist only because libft's `ft_putstr_fd` takes a
non-const pointer. The parameters are `const` here so callers can pass string
literals and `const char *` fields without their own casts; the cast is paid
once, inside the helper, instead of at every call site.

```c
ft_putendl_fd((char *)msg, STDERR_FILENO);
```
`ft_putendl_fd` rather than `ft_putstr_fd` so the newline is always there.
A diagnostic without a trailing newline would run into the next prompt.

There is no `printf` here and no `strerror` call: the caller passes
`strerror(errno)` as `msg` when it wants the system's wording (see
`redir_apply.c`), which keeps this function free of any errno handling of its
own. Using `write`-based output rather than `printf` also means the message
appears immediately, unbuffered, and interleaves correctly with a child's
output.

### `int is_space(char c)`

```c
return (c == ' ' || c == '\t' || c == '\n' || c == '\v'
    || c == '\f' || c == '\r');
```
The full POSIX whitespace set, matching `isspace()`. We write it out instead
of calling `isspace()` because `isspace` is not on the subject's list of
allowed functions — `tests/norm.sh` scans for exactly that kind of slip.

Why all six characters and not just space and tab? Because a `$VAR` whose
value contains a vertical tab or carriage return still word-splits in bash,
and this predicate is what the expander's splitter consults. Restricting it to
space and tab would silently produce a different argument vector from bash for
those inputs.

The parameter is `char`, not `int`, which is safe here because every caller
passes a character out of a `char *` buffer and we compare against literals
rather than indexing a table — the classic `isspace(negative char)` undefined
behaviour cannot happen.

### `int is_metachar(char c)`

```c
return (c == '|' || c == '<' || c == '>');
```
The three characters that end a word and begin an operator token. The lexer
calls this in two places: to stop scanning a word, and to decide that the next
token is an operator.

The set is deliberately small. Bash's metacharacter set also includes `;`,
`&`, `(`, `)` and `` ` ``, but the subject explicitly puts command separators,
background jobs, subshells and command substitution out of scope. Adding `;`
here would make `echo a;b` lex as two tokens and then fail in the parser with
a syntax error, whereas leaving it out makes `a;b` a single ordinary word —
which is exactly why `tests/edges.sh` never uses `;` in any test case. We
match the subject's scope, not bash's full grammar, and we do it by keeping
this list short.

`\` is absent for the same reason: backslash escaping is not required, so a
backslash is just another word character.

### `int line_is_blank(const char *line)`

```c
while (*line)
{
    if (!is_space(*line))
        return (0);
    line++;
}
return (1);
```
Returns true for the empty string and for any line made only of whitespace.
The early `return (0)` on the first non-space is the fast path — most real
lines exit on the first character.

`shell_loop` uses this before doing anything else with the line, and the
consequence is important: a blank line must not change `$?`. In bash,
pressing enter on an empty line leaves the previous exit status untouched, so
`false` then enter then `echo $?` still prints 1. Filtering here, before
`build_cmds`, is what preserves that. It also makes the `NULL` return from
`build_cmds` unambiguous — since blanks never reach it, a `NULL` can only mean
a syntax error, so `process_line` can set status 2 without further checks.

Note the pointer is walked directly rather than with an index. The parameter
is a local copy, so mutating it does not affect the caller's string.

## Things to be ready to explain

- **Why does `shell_error` take three strings instead of using `printf`?**
  `printf` is allowed, but the three-field form enforces bash's
  `name: context: arg: message` shape everywhere, and building it from `write`
  calls means output is unbuffered and cannot be reordered against a child's
  output.
- **Why write `is_space` by hand when `isspace` exists?** `isspace` is not in
  the subject's allowed-function list. `tests/norm.sh` greps every `.c` file
  for calls outside that list, so it would be caught.
- **Why is `;` not a metacharacter?** Command separators are outside the
  subject's scope. Leaving `;` out makes it an ordinary word character rather
  than a syntax error, and it is why `tests/edges.sh` never uses `;`.
- **Why must errors go to stderr?** So a failing command inside a pipeline
  does not inject its diagnostic into the data stream, and so the differential
  tester can compare stdout exactly while only checking stderr for presence.
- **Why check for a blank line in the main loop rather than in the lexer?**
  Two reasons: a blank line must leave `$?` unchanged, and filtering early
  makes a `NULL` from `build_cmds` mean "syntax error" and nothing else.
