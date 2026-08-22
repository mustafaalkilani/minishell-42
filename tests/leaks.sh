#!/bin/bash
# Runs a batch of scripts under valgrind and reports any leak or invalid
# access that is ours. readline's own allocations are suppressed, since the
# subject does not hold us responsible for them.
#
# --trace-children is on because a leak in the forked child (between fork
# and execve) is still our bug, and execve replaces the image so anything
# still held at that moment is lost.

ROOT=$(cd "$(dirname "$0")/.." && pwd)
MINISHELL=${MINISHELL:-$ROOT/minishell}
SUPP=$ROOT/tests/readline.supp
WORKDIR=$(mktemp -d)
FAIL=0

RED=$'\033[0;31m'
GREEN=$'\033[0;32m'
GREY=$'\033[38;5;244m'
BOLD=$'\033[1m'
END=$'\033[0m'

if [[ ! -x $MINISHELL ]]; then
	echo "${RED}no minishell binary at $MINISHELL — run make first${END}"
	exit 1
fi
if ! command -v valgrind >/dev/null; then
	echo "${RED}valgrind is not installed${END}"
	exit 1
fi

trap 'rm -rf "$WORKDIR"' EXIT

vg() {
	local desc="$1"; shift
	local log="$WORKDIR/vg.log"

	rm -rf "${WORKDIR:?}"/out
	mkdir -p "$WORKDIR/out"
	printf '%s\n' "$@" | (cd "$WORKDIR/out" && valgrind \
		--leak-check=full \
		--show-leak-kinds=definite,indirect \
		--errors-for-leak-kinds=definite,indirect \
		--track-origins=yes \
		--trace-children=yes \
		--suppressions="$SUPP" \
		--error-exitcode=42 \
		--log-file="$log" \
		"$MINISHELL") >/dev/null 2>&1

	# --error-exitcode only reports the parent, so also scan the log, which
	# covers the traced children.
	local errors
	errors=$(grep -c "^==[0-9]*== *\(definitely\|indirectly\) lost:" "$log")
	local invalid
	invalid=$(grep -c "Invalid \(read\|write\|free\)" "$log")

	if grep -q "definitely lost: [1-9]" "$log" \
		|| grep -q "indirectly lost: [1-9]" "$log" \
		|| ((invalid > 0)); then
		FAIL=$((FAIL + 1))
		printf '%s  KO  %s%s\n' "$RED" "$desc" "$END"
		grep -E "definitely lost|indirectly lost|Invalid (read|write|free)" "$log" \
			| sed "s/^/${GREY}      /;s/\$/${END}/" | sort -u
	else
		printf '%s  OK  %s%s%s\n' "$GREEN" "$GREY" "$desc" "$END"
	fi
}

printf '%svalgrind (readline suppressed, children traced)%s\n' "$BOLD" "$END"

vg "startup and clean exit" 'exit'
vg "builtins" 'echo hi' 'pwd' 'env > /dev/null' 'export A=1' 'unset A' 'cd /tmp' 'exit'
vg "export listing" 'export Z=1' 'export' 'exit'
vg "external command" '/bin/echo hi' 'exit'
vg "PATH lookup" 'ls > /dev/null' 'exit'
vg "command not found" 'no_such_cmd_xyz' 'exit'
vg "pipeline" 'echo a | cat | cat' 'exit'
vg "pipeline with a missing command" 'no_such_xyz | cat' 'exit'
vg "redirections" 'echo hi > f' 'cat < f' 'echo more >> f' 'cat f' 'exit'
vg "failed redirection" 'cat < no_such_file' 'echo hi > /no/such/dir/f' 'exit'
vg "heredoc" 'cat << EOF' 'body' 'EOF' 'exit'
vg "heredoc with expansion" 'export V=x' 'cat << EOF' 'v=$V' 'EOF' 'exit'
vg "quoted heredoc" 'cat << "EOF"' '$V' 'EOF' 'exit'
vg "syntax errors" 'echo a |' '| echo' 'echo >' "echo 'unclosed" 'exit'
vg "expansion and splitting" 'export S="a b c"' 'echo $S' 'echo "$S"' 'echo $?' 'exit'
vg "nested quotes" "echo \"a'b\" 'c\"d' ''\"\"" 'exit'
vg "exit with an argument" 'exit 3'
vg "exit with a bad argument" 'exit abc'
vg "long pipeline" 'echo a | cat | cat | cat | cat | cat | cat' 'exit'
vg "eof without exit" 'echo done'

printf '\n%s' "$BOLD"
if ((FAIL > 0)); then
	printf '%s%d leaking scenario(s)%s\n' "$RED" "$FAIL" "$END"
	exit 1
fi
printf '%sno leaks%s\n' "$GREEN" "$END"
exit 0
