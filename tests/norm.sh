#!/bin/bash
# Norminette plus the two rules it cannot see: one global only, and no
# forbidden function. The allowed list is the one printed in the subject.

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1
DIRS="includes srcs path-a path-b"
FAIL=0

RED=$'\033[0;31m'
GREEN=$'\033[0;32m'
GREY=$'\033[38;5;244m'
BOLD=$'\033[1m'
END=$'\033[0m'

printf '%snorminette%s\n' "$BOLD" "$END"
if ! command -v norminette >/dev/null; then
	printf '%s  norminette is not on PATH%s\n' "$RED" "$END"
	FAIL=1
else
	OUT=$(norminette $DIRS 2>&1)
	if grep -q "^Error" <<<"$OUT"; then
		FAIL=1
		grep -B1 "^Error" <<<"$OUT" | sed "s/^/${RED}  /;s/\$/${END}/"
	else
		printf '%s  %d files clean%s\n' "$GREEN" \
			"$(grep -c 'OK!' <<<"$OUT")" "$END"
	fi
fi

printf '\n%sglobals%s\n' "$BOLD" "$END"
GLOBALS=$(grep -rnE '^[A-Za-z_].*[A-Za-z_0-9]+ *(=|;)' --include='*.c' \
	$DIRS | grep -v '(' | grep -v '^\s*//')
COUNT=$(grep -c . <<<"${GLOBALS:-}")
[[ -z $GLOBALS ]] && COUNT=0
if ((COUNT > 1)); then
	FAIL=1
	printf '%s  %d globals, the subject allows one%s\n' "$RED" "$COUNT" "$END"
	sed "s/^/${GREY}  /;s/\$/${END}/" <<<"$GLOBALS"
else
	printf '%s  %d global%s\n' "$GREEN" "$COUNT" "$END"
	[[ -n $GLOBALS ]] && sed "s/^/${GREY}  /;s/\$/${END}/" <<<"$GLOBALS"
fi

printf '\n%sforbidden functions%s\n' "$BOLD" "$END"
ALLOWED="readline rl_clear_history rl_on_new_line rl_replace_line \
rl_redisplay add_history printf malloc free write access open read close \
fork wait waitpid wait3 wait4 signal sigaction sigemptyset sigaddset kill \
exit getcwd chdir stat lstat fstat unlink execve dup dup2 pipe opendir \
readdir closedir strerror perror isatty ttyname ttyslot ioctl getenv \
tcsetattr tcgetattr tgetent tgetflag tgetnum tgetstr tgoto tputs"
USED=$(grep -rhoE '\b[a-z_][a-z_0-9]*\(' --include='*.c' $DIRS \
	| tr -d '(' | sort -u)
KEYWORDS="sizeof if while for return switch"
BAD=""
for fn in $USED; do
	case " $ALLOWED $KEYWORDS " in
		*" $fn "*) continue ;;
	esac
	# Anything we define ourselves, or that libft provides, is fine.
	if grep -rqE "^[a-z_].*\b$fn\(" --include='*.c' $DIRS \
		|| grep -rqE "\b$fn\(" libft/libft.h 2>/dev/null; then
		continue
	fi
	BAD="$BAD $fn"
done
if [[ -n $BAD ]]; then
	FAIL=1
	printf '%s  not in the allowed list:%s%s\n' "$RED" "$BAD" "$END"
	printf '%s  (check these by hand — some are macros or libft helpers)%s\n' \
		"$GREY" "$END"
else
	printf '%s  none%s\n' "$GREEN" "$END"
fi

printf '\n'
exit $FAIL
