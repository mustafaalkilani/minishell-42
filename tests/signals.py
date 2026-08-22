#!/usr/bin/env python3
"""Interactive signal tests.

ctrl-C, ctrl-D and ctrl-\\ only behave the way the subject describes when
stdin is a terminal, so these cases cannot be piped like the others: the
shell is driven through a pty instead.

Run with:  python3 tests/signals.py
"""

import os
import pty
import re
import select
import signal
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MINISHELL = os.environ.get("MINISHELL", os.path.join(ROOT, "minishell"))

RED, GREEN, GREY, BOLD, END = (
    "\033[0;31m", "\033[0;32m", "\033[38;5;244m", "\033[1m", "\033[0m")

passed = 0
failed = []


class Shell:
    """A minishell running on its own pty, in its own process group."""

    def __init__(self):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.chdir("/tmp")
            os.execv(MINISHELL, [MINISHELL])
        self.buf = ""

    def read(self, timeout=0.4):
        deadline = time.time() + timeout
        while time.time() < deadline:
            ready, _, _ = select.select([self.fd], [], [], 0.05)
            if not ready:
                continue
            try:
                chunk = os.read(self.fd, 65536)
            except OSError:
                break
            if not chunk:
                break
            self.buf += chunk.decode(errors="replace")
        out, self.buf = self.buf, ""
        return out

    def send(self, text):
        os.write(self.fd, text.encode())
        time.sleep(0.15)

    def sig(self, signum):
        # The pty makes minishell a session leader, so the signal goes to
        # the whole foreground group exactly as a real ctrl-C would.
        try:
            os.killpg(os.getpgid(self.pid), signum)
        except ProcessLookupError:
            pass
        time.sleep(0.25)

    def close(self):
        try:
            os.close(self.fd)
        except OSError:
            pass
        try:
            os.kill(self.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            os.waitpid(self.pid, 0)
        except ChildProcessError:
            pass

    def wait_exit(self, timeout=2.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            pid, status = os.waitpid(self.pid, os.WNOHANG)
            if pid:
                if os.WIFEXITED(status):
                    return os.WEXITSTATUS(status)
                return 128 + os.WTERMSIG(status)
            time.sleep(0.05)
        return None


def report(desc, ok, detail=""):
    global passed
    if ok:
        passed += 1
        print(f"{GREEN}  OK  {GREY}{desc}{END}")
    else:
        failed.append(desc)
        print(f"{RED}  KO  {desc}{END}")
        if detail:
            print(f"{GREY}      {detail}{END}")


def strip_ansi(s):
    return re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", s)


def test_prompt_on_tty():
    sh = Shell()
    out = strip_ansi(sh.read())
    sh.close()
    report("a prompt is printed on a terminal", out.strip() != "",
           f"got {out!r}")


def test_ctrl_c_empty_line():
    sh = Shell()
    sh.read()
    sh.sig(signal.SIGINT)
    out = strip_ansi(sh.read())
    # bash moves to a fresh line and redraws the prompt, leaving no error.
    ok = "\n" in out and sh.wait_exit(0.3) is None
    sh.close()
    report("ctrl-C on an empty line redraws the prompt", ok, f"got {out!r}")


def test_ctrl_c_sets_status():
    sh = Shell()
    sh.read()
    sh.sig(signal.SIGINT)
    sh.read()
    sh.send("echo $?\n")
    out = strip_ansi(sh.read())
    sh.close()
    report("ctrl-C sets the exit status to 130", "130" in out, f"got {out!r}")


def test_ctrl_c_discards_typed_line():
    sh = Shell()
    sh.read()
    sh.send("echo should_not_run")
    sh.sig(signal.SIGINT)
    sh.read()
    sh.send("\n")
    out = strip_ansi(sh.read())
    sh.close()
    report("ctrl-C discards the half-typed line",
           "should_not_run" not in out, f"got {out!r}")


def test_ctrl_d_exits():
    sh = Shell()
    sh.read()
    sh.send("\x04")
    code = sh.wait_exit()
    out = strip_ansi(sh.buf)
    sh.close()
    report("ctrl-D on an empty line exits", code == 0,
           f"exit={code} out={out!r}")


def test_ctrl_d_keeps_status():
    sh = Shell()
    sh.read()
    sh.send("false\n")
    sh.read()
    sh.send("\x04")
    code = sh.wait_exit()
    sh.close()
    report("ctrl-D exits with the last status", code == 1, f"exit={code}")


def test_ctrl_backslash_ignored():
    sh = Shell()
    sh.read()
    sh.sig(signal.SIGQUIT)
    still_alive = sh.wait_exit(0.4) is None
    sh.send("echo alive\n")
    out = strip_ansi(sh.read())
    sh.close()
    report("ctrl-\\ is ignored by the shell itself",
           still_alive and "alive" in out, f"got {out!r}")


def test_ctrl_c_kills_child():
    sh = Shell()
    sh.read()
    sh.send("sleep 5\n")
    sh.sig(signal.SIGINT)
    sh.read()
    sh.send("echo $?\n")
    out = strip_ansi(sh.read())
    alive = sh.wait_exit(0.3) is None
    sh.close()
    report("ctrl-C kills the running child, shell survives with 130",
           alive and "130" in out, f"got {out!r}")


def test_ctrl_backslash_kills_child():
    sh = Shell()
    sh.read()
    sh.send("sleep 5\n")
    sh.sig(signal.SIGQUIT)
    sh.read()
    sh.send("echo $?\n")
    out = strip_ansi(sh.read())
    alive = sh.wait_exit(0.3) is None
    sh.close()
    report("ctrl-\\ kills the running child with 131",
           alive and "131" in out, f"got {out!r}")


def test_ctrl_c_during_heredoc():
    sh = Shell()
    sh.read()
    sh.send("cat << EOF\n")
    sh.send("some text\n")
    sh.sig(signal.SIGINT)
    sh.read()
    sh.send("echo back\n")
    out = strip_ansi(sh.read())
    alive = sh.wait_exit(0.3) is None
    sh.close()
    report("ctrl-C aborts a heredoc and returns to the prompt",
           alive and "back" in out, f"got {out!r}")


def test_ctrl_d_during_heredoc():
    sh = Shell()
    sh.read()
    sh.send("cat << EOF\n")
    sh.send("body\n")
    sh.send("\x04")
    out = strip_ansi(sh.read(1.0))
    alive = sh.wait_exit(0.3) is None
    sh.close()
    # bash warns and then runs the heredoc with what it already has.
    report("ctrl-D ends a heredoc without hanging",
           alive and "body" in out, f"got {out!r}")


def test_no_prompt_when_piped():
    out = subprocess.run([MINISHELL], input="echo hi\n", capture_output=True,
                         text=True).stdout
    report("no prompt is printed when stdin is a pipe", out == "hi\n",
           f"got {out!r}")


def main():
    if not os.access(MINISHELL, os.X_OK):
        print(f"{RED}no minishell binary at {MINISHELL}{END}")
        return 1
    print(f"{BOLD}interactive signal tests (pty){END}")
    for fn in (test_prompt_on_tty, test_ctrl_c_empty_line,
               test_ctrl_c_sets_status, test_ctrl_c_discards_typed_line,
               test_ctrl_d_exits, test_ctrl_d_keeps_status,
               test_ctrl_backslash_ignored, test_ctrl_c_kills_child,
               test_ctrl_backslash_kills_child, test_ctrl_c_during_heredoc,
               test_ctrl_d_during_heredoc, test_no_prompt_when_piped):
        try:
            fn()
        except Exception as exc:
            failed.append(fn.__name__)
            print(f"{RED}  KO  {fn.__name__}: {exc}{END}")
    print(f"\n{BOLD}{GREEN} passed: {passed}   failed: {len(failed)}{END}")
    if failed:
        for name in failed:
            print(f"  {name}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
