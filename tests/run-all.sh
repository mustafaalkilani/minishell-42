#!/bin/bash
# One entry point for every check: rebuild, norm, the two public testers and
# our own edge/leak suites. Each stage keeps running even if an earlier one
# failed, so a single run shows the whole picture.
#
#   bash tests/run-all.sh          everything
#   bash tests/run-all.sh quick    skip valgrind (which is the slow part)

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1
QUICK=${1:-}
FAILED=()

BOLD=$'\033[1m'
RED=$'\033[0;31m'
GREEN=$'\033[0;32m'
BLUE=$'\033[0;36m'
END=$'\033[0m'

stage() { printf '\n%s%s══ %s%s\n' "$BOLD" "$BLUE" "$1" "$END"; }
record() { [[ $1 -eq 0 ]] || FAILED+=("$2"); }

stage "build"
if make -s 2>&1 | tail -3; then
	printf '%s  built%s\n' "$GREEN" "$END"
else
	printf '%s  build failed%s\n' "$RED" "$END"
	exit 1
fi

stage "norminette, globals, forbidden functions"
bash tests/norm.sh
record $? "norm"

stage "edge cases (differential against bash)"
bash tests/edges.sh | tail -n 20
record "${PIPESTATUS[0]}" "edges"

stage "interactive signals (pty)"
if command -v python3 >/dev/null; then
	python3 tests/signals.py | tail -n 5
	record "${PIPESTATUS[0]}" "signals"
else
	printf '%s  python3 is needed to drive a pty%s\n' "$RED" "$END"
fi

stage "42_minishell_tester (zstenger93)"
if [[ -f testers/42_minishell_tester/tester.sh ]]; then
	bash testers/42_minishell_tester/tester.sh m 2>&1 | grep -E "TOTAL TEST COUNT"
else
	printf '%s  not installed%s\n' "$RED" "$END"
fi

stage "minishell_tester (LucasKuhn)"
if [[ -f testers/minishell_tester/tester ]]; then
	(cd testers/minishell_tester && bash tester 2>&1 | grep -oE "[0-9]+/[0-9]+$")
	(cd testers/minishell_tester && git clean -fq 2>/dev/null)
else
	printf '%s  not installed%s\n' "$RED" "$END"
fi

if [[ $QUICK != quick ]]; then
	stage "valgrind"
	bash tests/leaks.sh | tail -n 5
	record "${PIPESTATUS[0]}" "leaks"
fi

printf '\n%s' "$BOLD"
if ((${#FAILED[@]} > 0)); then
	printf '%sfailed stages: %s%s\n' "$RED" "${FAILED[*]}" "$END"
	exit 1
fi
printf '%sall stages passed%s\n' "$GREEN" "$END"
