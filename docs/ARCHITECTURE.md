# FishOS architecture

```
Fihcore kernel (submodule fihcore/, ring 0)
   │  int $0x80 system calls, mailboxes, shared memory, display ownership
   ├── init           (/system/bin/init)     prepares /home, restarts the desktop
   ├── desktop        (/system/bin/desktop)  display server + window manager + taskbar
   └── applications   (/system/bin/*)        each one a separate process
         files, edit, term, sysinfo, hexdump
```

## Repository layout

| Path | Contents |
|------|----------|
| `fihcore/` | Fihcore kernel, pinned Git submodule (the only copy of kernel code and ABI). |
| `lib/libc` | Minimal C library: strings, `printf`, `malloc` on `brk`, C++ runtime, file helpers (`fishos/fs.h`). |
| `lib/gfx` | Canvas drawing, alpha-blended bitmap text, generated fonts. |
| `lib/ui` | Toolkit: window connection, events, clipboard, buttons, line edit, dialogs, list view, icons. |
| `include/fishos` | Public FishOS headers, including the window protocol `wsproto.h`. |
| `system/init`, `system/desktop` | System programs. `system/apps/*.app` are launcher entries. |
| `apps/*` | Applications; every directory is one executable. |
| `tests/` | QMP driver and the end-to-end workflow test. |

## Boot and filesystem layout

GRUB loads `fihcore.elf` and the boot image `initrd.tar` (built from
`build/root`). The kernel mounts the image read-only at `/system` and the first
ATA disk containing FihFS at `/`. `init` is named on the kernel command line
(`init=/system/bin/init` in `grub.cfg`), so the kernel has no knowledge of FishOS.

| Path | Storage |
|------|---------|
| `/system/bin` | programs, read-only boot image |
| `/system/apps` | launcher descriptors (`name=`, `exec=`, `icon=`) |
| `/home` | user files on the data disk, created by `init` on first boot |

## Window system

The desktop acquires the framebuffer and all keyboard/mouse input with
`fih_display_acquire` and registers itself as `fishos.display`. Clients find it
with `fih_name_lookup`. Protocol (`include/fishos/wsproto.h`), one 64-byte
Fihcore message per request or event:

1. The client creates a shared memory buffer, grants it to the desktop and sends
   `WS_CREATE` with size, flags and title. The desktop checks the buffer size
   with `fih_shm_size` before mapping it, so a client cannot make it read past
   its buffer.
2. The client draws into its buffer and sends `WS_DAMAGE` rectangles; the
   desktop composites damaged regions (wallpaper, windows in z-order, taskbar,
   menu, cursor) into a back buffer and copies only those to the framebuffer.
3. Input arrives as `WS_EV_KEY` / `WS_EV_MOUSE` (window-local coordinates,
   double-click count computed centrally), plus `WS_EV_FOCUS`, `WS_EV_CLOSE`
   and `WS_EV_RESIZE`. On resize the client allocates a new buffer and answers
   with `WS_BUFFER`.
4. Only the owning process may modify a window. Windows of processes that exit
   or crash are removed by the desktop within a second.
5. The clipboard lives in the desktop: `WS_CLIP_SET` / `WS_CLIP_GET` move text
   through short-lived shared memory objects.

The desktop is event driven: it blocks in `fih_msg_recv` (with a one-second
timeout for the clock and liveness checks) and redraws only damaged areas.
Applications also block on events; none of them polls continuously except the
terminal while a child program runs (15 ms output polling).

## Applications

Every application links `libfishos.a` and Fihcore's `crt0.o`. Windows, input and
widgets come from `lib/ui`, so focus, selection colours, dialogs, scrolling and
clipboard behave the same everywhere. Modal dialogs are drawn inside the
application's own window.

- **Files** lists real directories (sorted, folders first), creates files and
  folders, renames, deletes (recursively, after confirmation), copies and moves
  (recursive copy, cross-filesystem move = copy + delete) and opens documents in
  the editor or runs programs from `/system/bin`.
- **Text Editor** keeps a line array, supports keyboard and mouse selection,
  word movement, clipboard, auto-indent, unsaved-change prompts on New, Open,
  close and logout. Saving writes `.<name>.saving` and renames it over the
  document (`FIH_RENAME_REPLACE`), so an interrupted save never truncates the
  original.
- **Terminal** implements the built-in commands listed by `help` directly on
  system calls; any other command is started from `/system/bin` with stdin and
  stdout/stderr connected through pipes. Programs listed as launcher entries
  start as separate windows instead.
- **System Monitor** shows kernel, CPU, memory, heap, storage and the process
  table and can end a process.

## Power

"Restart" and "Shut down" first send `WS_EV_CLOSE` to every window, so each
application can ask about unsaved work, and only power off once all windows are
gone. Clicking the notice in the taskbar cancels the request.

## Transitional parts

- Applications are separate processes, but every program is statically linked
  against `libfishos.a`; there are no shared libraries yet.
- The desktop process is display server, window manager, taskbar and launcher
  in one; splitting the shell from the compositor is possible because the
  protocol is message based.
- There is no notion of users or permissions.
