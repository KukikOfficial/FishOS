# FishOS

FishOS is a small desktop operating system built on the independent
[Fihcore](https://git.eclairs.app/eclair/Fihcore) kernel. Every part of the user
environment — the desktop, the window manager and each application — runs as a
separate, memory-isolated user-space process and talks to the kernel only
through Fihcore's documented system calls.

What you can do today:

- browse, create, rename, copy, move and delete files and folders (Files)
- write and edit text documents with selection, clipboard and safe saving (Text Editor)
- use a terminal with a built-in shell that also runs separate programs (Terminal)
- watch memory, storage and processes and stop programs (System Monitor)
- restart or shut down; everything in `/home` is kept on the data disk

## Quick start

```sh
git clone --recurse-submodules ssh://git@git.eclairs.app/eclair/FishOS.git
cd FishOS
make run        # builds everything, creates build/data.img once, boots QEMU
make test       # automated two-boot GUI workflow test
```

Requirements and details: [docs/BUILDING.md](docs/BUILDING.md).
How it fits together: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
What works and what does not: [docs/STATUS.md](docs/STATUS.md).

## Keyboard

| Keys | Action |
|------|--------|
| Ctrl+Esc or Super | open the FishOS menu |
| Alt+Tab | switch windows |
| Alt+F4 | close the active window |
| Files: Enter / Backspace / F2 / Delete | open / up / rename / delete |
| Files: Ctrl+N / Ctrl+Shift+N | new document / new folder |
| Files: Ctrl+C, Ctrl+X, Ctrl+V | copy, cut, paste files |
| Editor: Ctrl+S / Ctrl+Shift+S / Ctrl+O / Ctrl+N | save / save as / open / new |
| Editor: Ctrl+A, Ctrl+C, Ctrl+X, Ctrl+V | select all and clipboard |
| Terminal: Tab, Up/Down, Ctrl+C | complete, history, stop program |

## License

MIT, see [LICENSE](LICENSE). The bundled fonts are derived from Source Code Pro
and Lato under the SIL Open Font License, see
[third_party/fonts/LICENSE-OFL.txt](third_party/fonts/LICENSE-OFL.txt).
