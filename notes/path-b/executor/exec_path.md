# exec_path.c

Command-name resolution: turning `ls` into `/usr/bin/ls` by walking `PATH`, and
deciding when not to search at all. It also holds `free_split`, the companion
deallocator for every `ft_split` result in path-b. The subtle part is
`is_executable_file`, which exists because `access(X_OK)` says "yes" for
directories and would otherwise make `.` and `..` resolve to something
executable.

## Walkthrough

### `void free_split(char **split)`

```c
if (!split)
    return ;
```
Tolerating `NULL` means callers can free unconditionally without guarding at
every site — `child_exec` calls it on `envp` on the `execve`-failed path, and
`resolve_command` calls it on `paths`.

```c
i = 0;
while (split[i])
{
    free(split[i]);
    i++;
}
free(split);
```
Frees each string, then the array of pointers. The loop relies on the
NULL-terminated convention that `ft_split` and `env_to_envp` both produce; that
same terminator is what `execve` needs, so the representation serves two
purposes. Freeing the strings before the array is mandatory — after `free(split)`
the pointers would be unreachable and the strings would leak.

### `static int is_executable_file(const char *path)`

The heart of the file.

```c
struct stat st;

if (stat(path, &st) != 0 || S_ISDIR(st.st_mode))
    return (0);
```
`stat` failing means the candidate does not exist (or a path component is not a
directory), so this PATH entry is simply not a match — no error is printed,
because trying `/usr/local/bin/ls` and missing is the normal case, not a
failure.

The `S_ISDIR` test is the reason this helper exists at all. Directories carry
the execute bit with the meaning "searchable", so `access("/usr/bin/.", X_OK)`
returns 0. Without this check, typing `.` would resolve to `/usr/bin/.`,
`execve` would be called on a directory, and the shell would report 126 with
`Is a directory` instead of bash's 127 `command not found`. Rejecting
directories at resolution time pushes the case back onto the "nothing matched"
path, which is where it belongs.

`stat` is used rather than `lstat` deliberately: a symlink in `PATH` pointing at
a real binary must be accepted, and `stat` follows the link so `st_mode`
describes the target. `lstat` would see `S_IFLNK`, which is neither a directory
nor a regular file, and the test would still pass here — but the intent is to
judge the thing that will actually be executed.

```c
return (access(path, X_OK) == 0);
```
Only after the type check do we ask about permission. `access` uses the real
uid/gid rather than the effective one; for a shell that is not setuid the two
are identical, so this is fine and is the conventional choice.

There is an inherent race here: the file could be deleted or chmod'd between
this check and the `execve` in `child_exec`. That is unavoidable in any shell —
bash has the same window — and it is why `execve_failed` still has to handle
`EACCES` and `EISDIR` rather than assuming the check was authoritative.

### `static char *search_in_path(char *cmd, char **paths)`

```c
i = 0;
while (paths[i])
{
```
Walks the split `PATH` entries **in order** and stops at the first match. Order
is the whole semantics of `PATH`: if `/usr/local/bin` precedes `/usr/bin`, a
locally installed `ls` shadows the system one. Returning the first hit rather
than collecting all of them is what implements that rule.

```c
    temp = ft_strjoin(paths[i], "/");
    if (!temp)
        return (NULL);
    full_path = ft_strjoin(temp, cmd);
    free(temp);
    if (!full_path)
        return (NULL);
```
Two joins because `ft_strjoin` takes exactly two strings; the intermediate is
freed immediately after the second join consumes it, so at most one temporary is
alive at a time. On allocation failure the function returns `NULL`, which the
caller reports as `command not found` (127). That conflates out-of-memory with
not-found, which is a simplification, but the alternative — a distinct
out-of-memory status — is not something the subject asks for.

Note that this unconditionally inserts a `/`, so a `PATH` entry that already
ends in one produces `/usr/bin//ls`. That is harmless: the kernel collapses
repeated slashes, and bash does the same thing.

The empty `PATH` entry case falls out of this naturally only if `ft_split`
produces empty strings — it does not, since `ft_split` drops empty fields. So
`PATH=/bin::/usr/bin` does **not** get a "current directory" entry here, whereas
bash treats the empty field as `.`. The unset/empty-whole-variable case *is*
handled, in `resolve_command` below. Worth flagging as a known, narrow
divergence rather than claiming full parity.

```c
    if (is_executable_file(full_path))
        return (full_path);
    free(full_path);
    i++;
}
return (NULL);
```
On a hit the string is returned and ownership passes to the caller — which is
why `child_exec` has a `free(path)` on its failure path. On a miss the candidate
is freed before moving on, so a long `PATH` with no match allocates and releases
one string per entry and leaks nothing. Falling out of the loop returns `NULL`,
meaning "not found anywhere on PATH".

### `char *resolve_command(char *cmd, t_shell *sh)`

```c
if (!cmd || !*cmd)
    return (NULL);
```
An empty command name cannot be executed. This also protects the `ft_strchr`
below from a `NULL`. The caller turns this into 127.

```c
if (ft_strchr(cmd, '/'))
    return (ft_strdup(cmd));
```
The POSIX rule: a name containing a slash is a pathname, not a command name, and
is used verbatim without any `PATH` lookup. That is why `./a.out` and
`/bin/ls` work regardless of `PATH`, and why `PATH=` still lets you run
`/bin/echo`.

`ft_strdup` is used rather than returning `cmd` itself so that the caller always
owns a freeable string, no matter which branch produced it. A mixed ownership
contract — "sometimes free this, sometimes do not" — is how double-frees get
written.

Note this branch does **no** existence check. `./nope` returns a string, the
`execve` fails with `ENOENT`, and `execve_failed` prints
`No such file or directory` and exits 127 — which is exactly what bash prints
for that case, and is a better message than a flat `command not found`.

```c
path_env = env_get(sh->env, "PATH");
if (!path_env || !*path_env)
    path_env = ".";
```
`env_get` returns `NULL` when `PATH` was `unset`, and an empty string when it
was set to nothing. In bash an unset or empty `PATH` behaves as a single empty
entry, and an empty entry means the current directory — so `unset PATH; cd /bin;
./ls` is not the interesting case, but `unset PATH` followed by running a
program from the cwd is. Substituting `"."` reproduces that.

`path_env` is a borrowed pointer into the env list here, or a pointer to a string
literal in the substituted case. It is never freed, which is correct in both
cases, but it does mean the value must not be modified — and it is not: it is
only passed to `ft_split`, which copies.

```c
paths = ft_split(path_env, ':');
if (!paths)
    return (NULL);
result = search_in_path(cmd, paths);
free_split(paths);
return (result);
```
`ft_split` allocates a fresh array; `free_split` releases it before returning, so
the only surviving allocation is the resolved path itself. The split happens per
resolution rather than being cached, which is slightly wasteful but is also what
makes `export PATH=...` take effect on the very next command with no
invalidation logic.

`result` may be `NULL`, and the caller (`child_exec`) treats that as
`command not found` / 127.

## Things to be ready to explain

- **Why is `access(X_OK)` not enough on its own?** Because directories have the
  execute bit set, meaning "searchable". `access("/usr/bin/.", X_OK)` succeeds,
  so `.` would resolve to a directory, `execve` would fail, and the shell would
  report 126 instead of bash's 127. The `stat` plus `S_ISDIR` rejection in
  `is_executable_file` is what pushes that case back to "not found".
- **Why does a name with a `/` skip the search entirely?** POSIX says a
  slash-containing word is a pathname, not a command name. It is what makes
  `./a.out` run the local binary rather than hunting for one called `./a.out`
  inside every `PATH` directory.
- **What does an unset or empty `PATH` mean?** One empty entry, and an empty
  entry names the current directory, so the code substitutes `"."`. Note the
  narrower case `PATH=/bin::/usr/bin` is *not* handled the same way, because
  `ft_split` discards empty fields — a known small divergence from bash.
- **Who frees the string `resolve_command` returns?** The caller. `child_exec`
  frees it on the `execve`-failed path; on success `execve` destroys the address
  space, so there is nothing to free.
- **Why walk `PATH` in order instead of collecting all matches?** Because
  shadowing is the point of `PATH` ordering: the first executable match wins,
  which is how a build directory earlier in `PATH` overrides a system binary.
- **Is there a TOCTOU race between the check and the exec?** Yes, and it is
  unavoidable — bash has it too. That is why `execve_failed` still handles
  `EACCES` and directories rather than trusting `is_executable_file`.
