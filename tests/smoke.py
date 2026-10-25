#!/usr/bin/env python3
"""End-to-end FishOS workflow test.

Boots FishOS in QEMU twice with a fresh disposable data disk and drives the
real GUI through QMP (keyboard and mouse), then checks the results on the disk
image from the host with fihfs-tool. Screenshots go to BUILD/test/smoke-*.png.
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
from qmp import QMP  # noqa: E402

B = sys.argv[1] if len(sys.argv) > 1 else "build"
T = os.path.join(B, "test")
DISK = os.path.join(T, "smoke.img")
LOG = os.path.join(T, "serial.log")
TOOL = "fihcore/build/host/fihfs-tool"
QEMU = os.environ.get("QEMU", "qemu-system-i386")
TEXT = "Shopping list\n- fish food\n- coffee beans\nTODO: call Alice"
failures = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        failures.append(what)


def boot():
    sock = os.path.join(T, "qmp.sock")
    for p in (sock, LOG):
        if os.path.exists(p):
            os.remove(p)
    proc = subprocess.Popen([QEMU, "-m", "256M", "-cdrom", os.path.join(B, "fishos.iso"), "-boot", "d",
                             "-vga", "std", "-drive", f"file={DISK},format=raw,if=ide,index=0",
                             "-serial", f"file:{LOG}", "-display", "none", "-no-reboot",
                             "-qmp", f"unix:{sock},server,nowait"])
    q = QMP(sock)
    wait_log("desktop: running")
    return proc, q


def log():
    try:
        with open(LOG, errors="replace") as f:
            return f.read()
    except FileNotFoundError:
        return ""


def wait_log(text, timeout=30, count=1):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if log().count(text) >= count:
            return True
        time.sleep(0.2)
    return False


def host_cat(path):
    r = subprocess.run([TOOL, "cat", DISK, path], capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


def launch(q, index):
    """Opens the n-th application of the (alphabetical) launcher menu."""
    q.key("ctrl-esc")
    time.sleep(0.4)
    for _ in range(index):
        q.key("down")
    q.key("ret")


def main():
    os.makedirs(T, exist_ok=True)
    if os.path.exists(DISK):
        os.remove(DISK)
    subprocess.run([TOOL, "mkfs", DISK, "32", "smoke"], check=True)

    # ---- first boot: create, edit, save, reopen, restart ----------------------
    proc, q = boot()
    check("created /home on first boot" in log(), "first boot prepares /home")
    launch(q, 0)  # Files
    check(wait_log("'files'"), "Files starts")
    time.sleep(1)
    q.key("ctrl-shift-n"); time.sleep(0.4)
    q.type("Notes"); q.key("ret"); time.sleep(0.6)
    q.key("ret"); time.sleep(0.6)            # enter the new folder
    q.key("ctrl-n"); time.sleep(0.4)
    q.type("plan"); q.key("ret"); time.sleep(0.6)
    q.key("ret")                             # open the document
    check(wait_log("'edit'"), "document opens in the Text Editor")
    time.sleep(1.2)
    q.type("Shopping list\n- fish food\n- coffee\nTODO: call Bob")
    for _ in range(3):
        q.key("backspace")
    q.type("Alice")
    q.key("up"); q.key("end"); q.type(" beans")
    q.screenshot(os.path.join(T, "smoke-editor.png"))
    q.key("ctrl-s"); time.sleep(0.8)
    q.key("alt-f4")
    check(wait_log("'edit' exited with 0"), "editor closes after saving")
    time.sleep(0.6)
    q.key("ret")                             # reopen from Files
    check(wait_log("'edit'", count=2), "document reopens")
    time.sleep(1.2)
    q.screenshot(os.path.join(T, "smoke-reopen.png"))
    q.key("alt-f4"); time.sleep(0.6)
    # File operations in Files (plan.txt is selected).
    q.key("ctrl-c"); q.key("ctrl-v"); time.sleep(0.8)   # -> "plan 2.txt", selected
    q.key("f2"); time.sleep(0.4)
    q.type("archive"); q.key("ret"); time.sleep(0.6)     # -> archive.txt
    q.key("ctrl-shift-n"); time.sleep(0.4)
    q.type("Old"); q.key("ret"); time.sleep(0.6)
    q.key("a"); q.key("ctrl-x"); time.sleep(0.3)        # select archive.txt, cut
    q.key("o"); q.key("ret"); time.sleep(0.6)           # into Old
    q.key("ctrl-v"); time.sleep(0.8)
    q.screenshot(os.path.join(T, "smoke-files.png"))
    q.key("backspace"); time.sleep(0.6)                 # back up to Notes, Old selected
    q.key("ctrl-c"); q.key("ctrl-v"); time.sleep(0.8)   # copy folder -> "Old 2"
    # The kernel writes through to the disk image, so the host can read it now.
    check(host_cat("/home/Notes/Old 2/archive.txt") == TEXT, "folder copied recursively")
    q.key("delete"); time.sleep(0.4)
    q.key("ret"); time.sleep(0.8)                       # confirm recursive delete of "Old 2"
    launch(q, 4)                             # Restart (4 apps, then Restart)
    proc.wait(timeout=30)
    check("power: request 1" in log(), "Restart closes the session and reboots")

    content = host_cat("/home/Notes/plan.txt")
    check(content == TEXT, "saved document has the expected content on disk")
    check(host_cat("/home/Notes/Old/archive.txt") == TEXT, "copy, rename and cut/paste into a folder")
    check(host_cat("/home/Notes/archive.txt") is None and host_cat("/home/Notes/plan 2.txt") is None,
          "moved file left its old folder")
    check(host_cat("/home/Notes/Old 2/archive.txt") is None, "copied folder deleted recursively")
    fsck = subprocess.run([TOOL, "fsck", DISK], capture_output=True, text=True)
    check(fsck.returncode == 0, "filesystem is consistent after the session")

    # ---- second boot: the document survived; read it from the terminal ----
    proc, q = boot()
    check("created /home" not in log(), "second boot keeps the existing /home")
    launch(q, 2)                             # Terminal
    check(wait_log("'term'"), "Terminal starts")
    time.sleep(1.2)
    q.type("cd /home/Notes\n")
    q.type("cat plan.txt\n")
    q.type("cat plan.txt > from-terminal.txt\n")
    q.type("hexdump plan.txt\n")
    check(wait_log("'hexdump' exited with 0"), "external program runs from the terminal")
    time.sleep(0.5)
    q.screenshot(os.path.join(T, "smoke-terminal.png"))
    q.type("shutdown\n")
    try:
        proc.wait(timeout=20)
    except subprocess.TimeoutExpired:
        q.quit()
        proc.wait(timeout=10)
    check(host_cat("/home/Notes/from-terminal.txt") == TEXT, "terminal read the document after reboot")
    fsck = subprocess.run([TOOL, "fsck", DISK], capture_output=True, text=True)
    check(fsck.returncode == 0, "filesystem is consistent after the second session")
    check("PANIC" not in log() and "fault:" not in log(), "no kernel panic or process fault")

    print(f"smoke: {'FAILED' if failures else 'PASS'} ({len(failures)} failure(s))")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
