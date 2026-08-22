#!/bin/bash
# Edge-case differential tester: feeds the same script to ./minishell and to
# bash, then compares stdout and the exit code. stderr is compared only by
# presence (empty vs non-empty), because the "minishell:" prefix is allowed
# to differ from bash's "bash: line N:".
#
# Usage:  check <description> <line> [line ...]
# Each extra argument is one input line. ';' is deliberately never used:
# the subject leaves command separators out of scope.

MINISHELL=${MINISHELL:-$(cd "$(dirname "$0")/.." && pwd)/minishell}
WORKDIR=$(mktemp -d)
PASS=0
FAIL=0
FAILED_CASES=()

RED=$'\033[0;31m'
GREEN=$'\033[0;32m'
GREY=$'\033[38;5;244m'
BOLD=$'\033[1m'
END=$'\033[0m'

if [[ ! -x $MINISHELL ]]; then
	echo "${RED}no minishell binary at $MINISHELL — run make first${END}"
	exit 1
fi

cleanup() { chmod -R u+rwX "$WORKDIR" 2>/dev/null; rm -rf "$WORKDIR"; }
trap cleanup EXIT

run_one() {
	# $1 = shell to run, rest = input lines. Echoes "<code>|<haserr>" and
	# leaves stdout in the global RUN_OUT.
	local sh="$1"; shift
	chmod -R u+rwX "$WORKDIR" 2>/dev/null
	rm -rf "${WORKDIR:?}"/*
	RUN_OUT=$(cd "$WORKDIR" && printf '%s\n' "$@" | "$sh" 2>"$WORKDIR/.err")
	local code=$?
	local haserr=0
	[[ -s "$WORKDIR/.err" ]] && haserr=1
	rm -f "$WORKDIR/.err"
	echo "$code|$haserr"
}

check() {
	local desc="$1"; shift
	local m b m_out b_out

	m=$(run_one "$MINISHELL" "$@"); m_out=$RUN_OUT
	b=$(run_one /bin/bash "$@"); b_out=$RUN_OUT

	local why=""
	[[ $m_out != "$b_out" ]] && why+="stdout "
	[[ ${m%|*} != "${b%|*}" ]] && why+="exit(${m%|*}!=${b%|*}) "
	[[ ${m#*|} != "${b#*|}" ]] && why+="stderr "

	if [[ -z $why ]]; then
		PASS=$((PASS + 1))
		printf '%s  OK  %s%s%s\n' "$GREEN" "$GREY" "$desc" "$END"
	else
		FAIL=$((FAIL + 1))
		FAILED_CASES+=("$desc [$why]")
		printf '%s  KO  %s %s(%s)%s\n' "$RED" "$desc" "$GREY" "$why" "$END"
		printf '%s      mini: %q\n      bash: %q%s\n' "$GREY" "$m_out" "$b_out" "$END"
	fi
}

section() { printf '\n%s— %s%s\n' "$BOLD" "$1" "$END"; }

section "quoting"
check "empty single quotes kept as an argument" "echo '' | cat -A"
check "empty double quotes kept as an argument" 'echo "" | cat -A'
check "adjacent quotes glue into one word" "echo a''b\"\"c"
check "single quotes suppress expansion" "echo '\$HOME \$? \$USER'"
check "double quotes keep spacing" 'echo "a     b"'
check "quote inside the other quote" "echo \"it's\" 'say \"hi\"'"
check "quoted operator is not an operator" 'echo "a | b" "c > d"'
check "empty command from empty quotes" '""'
check "quoted command name still runs" '"echo" hi'

section "variable expansion"
check "unset variable expands to nothing" 'echo [$NO_SUCH_VAR_XYZ]'
check "unset variable disappears entirely" 'echo a $NO_SUCH_VAR_XYZ b'
check "quoted unset variable keeps the field" 'echo "[$NO_SUCH_VAR_XYZ]"'
check "lone dollar stays literal" 'echo $'
check "dollar then digit" 'echo $1x'
check "name stops at a quote boundary" 'export T=ech' 'echo $T"o" ok'
check "dollar before quotes is dropped" 'echo $"HOME"'
check "dollar before empty quotes is dropped" 'echo $""x'
check "dollar after a closing quote stays" 'echo "$"USER'
check "exit status of true" 'true' 'echo $?'
check "exit status of false" 'false' 'echo $?'
check "exit status of a missing command" 'no_such_cmd_xyz' 'echo $?'
check "exit status of a builtin failure" 'cd /no/such/dir' 'echo $?'
check "status survives into the next word" 'false' 'echo "code=$?"'
check "word splitting of an unquoted expansion" 'export S="a b c"' 'echo [$S]'
check "no splitting when quoted" 'export S="a b c"' 'echo "[$S]"'
check "expansion that is only spaces vanishes" 'export S="   "' 'echo a $S b'
check "expansion inside double quotes keeps spaces" 'export S="  x  "' 'echo "[$S]"'

section "builtins"
check "echo -n suppresses the newline" 'echo -n hi'
check "echo with stacked n flags" 'echo -nnn hi'
check "echo -n-ish word is not a flag" 'echo -na hi'
check "echo with no argument" 'echo'
check "pwd matches the shell cwd" 'pwd'
check "cd with no argument goes home" 'cd' 'pwd'
check "cd - prints and returns" 'cd /tmp' 'cd -' 'pwd'
check "cd -- means home" 'cd /tmp' 'cd --' 'pwd'
check "cd with too many arguments" 'cd a b' 'echo $?'
check "cd rejects an unknown option" 'cd -x' 'echo $?'
check "cd -- ends option parsing" 'cd -- -x' 'echo $?'
check "cd into a file" 'echo x > f' 'cd f' 'echo $?'
check "export without a value is not exported" 'export ONLYNAME' 'env | grep -c ONLYNAME'
check "export appends with +=" 'export A=1' 'export A+=2' 'echo $A'
check "export += creates when unset" 'export NEWV+=abc' 'echo $NEWV'
check "export rejects a bad identifier" 'export 1BAD=x' 'echo $?'
check "export rejects an unknown option" 'export -z' 'echo $?'
check "export stops options at the first operand" 'export A=1 -z' 'echo $?'
check "unset ignores a bad identifier" 'unset 1BAD' 'echo $?'
check "unset removes the variable" 'export A=1' 'unset A' 'echo [$A]'
check "unset rejects an option" 'unset -A' 'echo $?'
check "unset stops options at the first operand" 'unset A -z' 'echo $?'
check "unset with no argument" 'unset' 'echo $?'
check "env rejects an option" 'env -x' 'echo $?'
check "exit with no argument uses last status" 'false' 'exit'
check "exit with a numeric argument" 'exit 42'
check "exit wraps modulo 256" 'exit 300'
check "exit with a negative number" 'exit -1'
check "exit with a plus sign" 'exit +5'
check "exit rejects a non-numeric argument" 'exit abc'
check "exit with too many arguments" 'exit 1 2'
check "exit with a huge number" 'exit 99999999999999999999'
check "exit ignores surrounding blanks" 'exit " 42 "'
check "exit rejects trailing junk" 'exit "42 x"'
check "exit rejects an empty argument" 'exit ""'

section "redirections"
check "write then read back" 'echo hi > f' 'cat f'
check "append does not truncate" 'echo a > f' 'echo b >> f' 'cat f'
check "truncate resets the file" 'echo aaa > f' 'echo b > f' 'cat f'
check "last output redirection wins" 'echo hi > f1 > f2' 'cat f2' 'wc -c < f1'
check "all redirections are created" 'echo hi > f1 > f2' 'ls f1 f2'
check "input from a missing file" 'cat < no_such_file' 'echo $?'
check "output into a missing directory" 'echo hi > no/such/dir/f' 'echo $?'
check "redirection before the command" '> f echo hi' 'cat f'
check "redirection between arguments" 'echo a > f b' 'cat f'
check "read and write in one command" 'echo src > in' 'cat < in > out' 'cat out'
check "input redirection order matters" 'echo one > a' 'echo two > b' 'cat < a < b'
check "heredoc feeds stdin" 'cat << EOF' 'line1' 'line2' 'EOF'
check "heredoc expands variables" 'export V=x' 'cat << EOF' 'v=$V' 'EOF'
check "quoted heredoc delimiter stays literal" 'export V=x' 'cat << "EOF"' 'v=$V' 'EOF'
check "single-quoted heredoc delimiter" 'export V=x' "cat << 'EOF'" 'v=$V' 'EOF'
check "heredoc delimiter must match exactly" 'cat << EOF' 'EOFX' 'EOF'
check "two heredocs, last one wins on stdin" 'cat << A << B' 'first' 'A' 'second' 'B'
check "heredoc into a pipe" 'cat << EOF | tr a-z A-Z' 'hello' 'EOF'
check "heredoc plus output redirection" 'cat << EOF > f' 'body' 'EOF' 'cat f'
check "empty heredoc" 'cat << EOF' 'EOF'
check "heredoc body is not tilde-expanded" 'cat << EOF' '~/x' 'EOF'
check "heredocs leak no fds into a child" 'cat << A | cat << B | ls /proc/self/fd' 'x' 'A' 'y' 'B'

section "pipes"
check "two-stage pipe" 'echo hello | cat'
check "exit code comes from the last stage" 'false | true' 'echo $?'
check "exit code of a failing last stage" 'true | false' 'echo $?'
check "long pipeline" 'echo a | cat | cat | cat | cat | cat'
check "builtin in the middle of a pipe" 'echo x | cd /tmp | cat' 'echo $?'
check "builtin output through a pipe" 'export V=1' 'env | grep -c "^V=1$"'
check "cd in a pipe does not move the shell" 'pwd > before' 'cd /tmp | cat' 'pwd'
check "missing command in a pipeline" 'no_such_xyz | echo after' 'echo $?'
check "pipe with redirection on one side" 'echo hi > f' 'cat < f | cat'
check "head closes the pipe early" 'yes | head -n 2'
check "exit inside a pipeline" 'exit 3 | echo done' 'echo $?'

section "syntax errors"
check "trailing pipe" 'echo a |'
check "leading pipe" '| echo a'
check "pipe followed by a pipe" 'echo a | | echo b'
check "redirection with no target" 'echo a >'
check "double redirection operator" 'echo a > > b'
check "unclosed single quote" "echo 'abc"
check "unclosed double quote" 'echo "abc'
check "only whitespace is not an error" '   '
check "empty line is not an error" ''
check "tabs only" $'\t\t'

section "command resolution"
check "absolute path runs" '/bin/echo hi'
check "dot is not executable" '.' 'echo $?'
check "dot dot is not executable" '..' 'echo $?'
check "directory as a command" '/tmp' 'echo $?'
check "permission denied" 'echo x > f' 'chmod 000 f' './f' 'echo $?'
check "command not found exit code" 'no_such_cmd_xyz' 'echo $?'
check "empty PATH falls back to cwd" 'unset PATH' 'cd /bin' 'ls > /dev/null' 'echo $?'
check "PATH with an empty entry" 'export PATH=":/bin"' 'echo ok'
check "very long argument survives" 'echo aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'

printf '\n%s%s passed: %d   failed: %d%s\n' "$BOLD" "$GREEN" "$PASS" "$FAIL" "$END"
if ((FAIL > 0)); then
	printf '%sfailed cases:%s\n' "$RED" "$END"
	printf '  %s\n' "${FAILED_CASES[@]}"
	exit 1
fi
exit 0
