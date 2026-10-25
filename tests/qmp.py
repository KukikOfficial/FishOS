#!/usr/bin/env python3
"""Minimal QMP client used by the FishOS smoke test to drive QEMU:
keyboard input, relative mouse movement, clicks and screenshots."""
import json
import os
import socket
import time

KEYMAP = {" ": "spc", "\n": "ret", "-": "minus", "=": "equal", ".": "dot", ",": "comma",
          "/": "slash", ";": "semicolon", "'": "apostrophe", "[": "bracket_left",
          "]": "bracket_right", "\\": "backslash", "`": "grave_accent"}
SHIFTED = {"_": "minus", "+": "equal", ">": "dot", "<": "comma", "?": "slash", ":": "semicolon",
           '"': "apostrophe", "{": "bracket_left", "}": "bracket_right", "|": "backslash",
           "~": "grave_accent", "!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6",
           "&": "7", "*": "8", "(": "9", ")": "0"}


class QMP:
    def __init__(self, path, timeout=30):
        deadline = time.time() + timeout
        while True:
            try:
                self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.sock.connect(path)
                break
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.2)
        self.file = self.sock.makefile("rw")
        self._read()  # greeting
        self.cmd("qmp_capabilities")

    def _read(self):
        while True:
            msg = json.loads(self.file.readline())
            if "event" not in msg:
                return msg

    def cmd(self, name, **args):
        self.file.write(json.dumps({"execute": name, "arguments": args}) + "\n")
        self.file.flush()
        reply = self._read()
        if "error" in reply:
            raise RuntimeError(f"{name}: {reply['error']}")
        return reply.get("return")

    def key(self, combo, hold_ms=40):
        """combo like 'ctrl-s', 'alt-tab', 'ret', 'a'."""
        keys = [{"type": "qcode", "data": k} for k in combo.split("-")]
        self.cmd("send-key", keys=keys, **{"hold-time": hold_ms})
        time.sleep(0.08)

    def type(self, text):
        for ch in text:
            if ch.isalpha() and ch.isupper():
                self.key("shift-" + ch.lower())
            elif ch in SHIFTED:
                self.key("shift-" + SHIFTED[ch])
            else:
                self.key(KEYMAP.get(ch, ch))

    def move(self, dx, dy):
        # PS/2 packets carry at most +-255; split large moves.
        while dx or dy:
            sx = max(-120, min(120, dx))
            sy = max(-120, min(120, dy))
            self.cmd("input-send-event", events=[
                {"type": "rel", "data": {"axis": "x", "value": sx}},
                {"type": "rel", "data": {"axis": "y", "value": sy}}])
            dx -= sx
            dy -= sy
            time.sleep(0.03)

    def button(self, down, btn="left"):
        self.cmd("input-send-event", events=[{"type": "btn", "data": {"down": down, "button": btn}}])
        time.sleep(0.05)

    def click(self, btn="left"):
        self.button(True, btn)
        self.button(False, btn)
        time.sleep(0.1)

    def double_click(self):
        self.button(True)
        self.button(False)
        self.button(True)
        self.button(False)
        time.sleep(0.15)

    def wheel(self, steps):
        btn = "wheel-down" if steps > 0 else "wheel-up"
        for _ in range(abs(steps)):
            self.button(True, btn)
            self.button(False, btn)

    def screenshot(self, png_path):
        ppm = png_path + ".ppm"
        self.cmd("screendump", filename=os.path.abspath(ppm))
        time.sleep(0.2)
        try:
            from PIL import Image
            Image.open(ppm).save(png_path)
            os.remove(ppm)
        except ImportError:
            os.replace(ppm, png_path[:-4] + ".ppm")

    def quit(self):
        try:
            self.cmd("quit")
        except Exception:
            pass
