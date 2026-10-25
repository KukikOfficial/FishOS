# Building and running FishOS

## Requirements

- `i686-elf-gcc`/`g++` cross compiler (`CROSS=` to override the prefix)
- GRUB 2 `grub-mkrescue` (or `i686-elf-grub-mkrescue`) and `xorriso`
- `qemu-system-i386`
- a host C++ compiler (builds Fihcore's `fihfs-tool`)
- Python 3 for `make test` (Pillow optional, for PNG screenshots;
  `tools/genfont.py` needs Pillow but its output is committed)

macOS (Homebrew): `brew install i686-elf-gcc i686-elf-grub xorriso qemu`.

## Checkout

```sh
git clone --recurse-submodules ssh://git@git.eclairs.app/eclair/FishOS.git
# or, in an existing clone:
git submodule update --init
```

The submodule pins the exact Fihcore revision FishOS is tested with.

## Commands

```sh
make             # kernel (via fihcore/), user space, build/fishos.iso
make disk        # create build/data.img (32 MiB FihFS) unless it exists
make run         # make + disk, then boot QEMU (serial log on stdout)
make test        # end-to-end test, see below
make clean       # removes build/ (keeps nothing, including data.img)
```

`make run` uses:

```sh
qemu-system-i386 -m 256M -cdrom build/fishos.iso -boot d -vga std \
    -drive file=build/data.img,format=raw,if=ide,index=0 -serial stdio
```

The data disk is never recreated or formatted by the build or the kernel.
To start over, delete `build/data.img` yourself and run `make disk`.
Inspect it from the host with `fihcore/build/host/fihfs-tool ls build/data.img /home`.

## Tests

- `make test` (FishOS): boots twice with a fresh disposable disk
  (`build/test/smoke.img`), drives the GUI through QMP — open Files, create a
  folder and a document, edit and save in the editor, close and reopen, restart
  from the menu, then read the document from the terminal and run an external
  program — and checks file contents and `fsck` from the host. Screenshots:
  `build/test/smoke-*.png`, serial log: `build/test/serial.log`.
- `make -C fihcore test-host test`: kernel and filesystem tests.
