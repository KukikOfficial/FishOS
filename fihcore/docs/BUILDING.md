# Building and testing Fihcore

## Requirements

- Cross compiler: `i686-elf-gcc` / `i686-elf-g++` (override with `CROSS=`)
- GRUB 2 with `grub-mkrescue` (or `i686-elf-grub-mkrescue`) and `xorriso`
- `qemu-system-i386`
- A host C++17 compiler (for `fihfs-tool` and unit tests)

macOS (Homebrew): `brew install i686-elf-gcc i686-elf-grub xorriso qemu`.

## Commands

```sh
make              # build/fihcore.elf, build/user/crt0.o
make tools        # build/host/fihfs-tool
make test-host    # FihFS unit tests with ASan/UBSan
make test         # QEMU integration test (two boots, persistence, fsck)
make clean
```

`make test` creates a disposable 8 MiB disk image under `build/test/`, boots
`build/test/fihcore-test.iso` twice and checks `build/test/serial.log` for
`KTEST RESULT: PASS` and `KTEST PERSIST: PASS`, then runs `fihfs-tool fsck`.

## Disk images

```sh
build/host/fihfs-tool mkfs data.img 32 mylabel   # refuses to overwrite without -f
build/host/fihfs-tool put data.img notes.txt /notes.txt
build/host/fihfs-tool ls data.img /
build/host/fihfs-tool fsck data.img
```

Attach as the first IDE disk: `-drive file=data.img,format=raw,if=ide,index=0`.
