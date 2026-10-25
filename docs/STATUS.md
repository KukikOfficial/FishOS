# FishOS status

**Verified** = covered by `make test` (end-to-end, real GUI input in QEMU) or
checked manually in QEMU on 2026-10-25 as noted. **Implemented** = works in
use but has no automated check.

| Feature | State | Notes |
|---------|-------|-------|
| Boot to desktop from ISO | verified | |
| Separate processes for desktop and every app | verified | Fihcore ring 3, own address spaces |
| Launcher from `/system/apps` descriptors | verified | |
| Window focus, raise, close (Alt+F4) | verified | |
| Window move, maximize (double-click), minimize, taskbar restore | verified manually | |
| Window resize by grip | implemented | same protocol as maximize |
| Alt+Tab | implemented | |
| Removing windows of crashed/killed apps | verified manually | System Monitor "End Process" |
| Files: navigate, new folder, new document, open | verified | |
| Files: copy/paste, rename, cut/paste into folder, recursive copy and delete | verified | |
| Text editor: typing, newline, backspace, arrows, save, reopen | verified | |
| Editor: selection, clipboard, Save As, unsaved prompt | implemented | |
| Persistence across reboot | verified | host-side content check + fsck |
| Terminal built-ins (`cd`, `cat`, `ls`, redirection, …) | verified | |
| Terminal runs external programs via pipes | verified | `hexdump` |
| System Monitor | verified manually | |
| Restart / shut down after closing apps | verified | shutdown works on QEMU/Bochs/VirtualBox |
| Clipboard (text) between apps | implemented | |

## Known limitations

- No undo in the text editor; tabs are expanded to spaces when a file is loaded;
  files are limited to 4 MiB (FihFS limit); only ASCII text.
- The terminal has no mouse selection; `>` redirection only applies to built-ins.
- No hover feedback (the desktop only forwards pointer motion while a button is held).
- Keyboard layout is US only; no key repeat beyond what the PS/2 controller sends.
- Programs are statically linked; there is no shared library loader yet.
- No users or file permissions.
