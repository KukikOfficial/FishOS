# Fihcore

Fihcore is a small, independent 32-bit x86 kernel. It boots through Multiboot
(GRUB), runs user programs as isolated ring-3 processes with their own address
spaces, and exposes a documented system call interface. It knows nothing about
any particular desktop or user environment; [FishOS](https://git.eclairs.app/eclair/FishOS)
is the first operating system built on top of it.

## Features

- Multiboot boot, GDT/TSS, IDT with exception diagnostics and kernel panic screen
- Serial (COM1) logging, framebuffer boot console
- Physical frame allocator from the bootloader memory map, paging with per-process
  address spaces, guarded null page, kernel heap with corruption checks
- Preemptive round-robin scheduler, process creation from static ELF executables,
  exit/wait/kill, demand-grown user stacks, x87 state switching
- System calls via `int $0x80` (`include/fihcore/abi.h`)
- File descriptors, pipes, per-process working directory
- IPC: per-process mailboxes, shared memory with explicit grants, a service name registry
- Exclusive display/input ownership for a user-space display server
- Drivers: PIT, CMOS RTC, PS/2 keyboard and mouse (with wheel), ATA PIO with timeouts
- Filesystems: FihFS (native on-disk filesystem), read-only tarfs for the boot image, VFS with mounts

## Quick start

Requirements: `i686-elf-gcc`, GRUB (`i686-elf-grub-mkrescue` or `grub-mkrescue`),
`xorriso`, `qemu-system-i386`, a host C++ compiler.

```sh
make            # build/fihcore.elf and build/user/crt0.o
make test-host  # FihFS unit tests (host, ASan/UBSan)
make test       # boots the kernel in QEMU with an integration test as init
```

See [docs/BUILDING.md](docs/BUILDING.md), [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
and [docs/STATUS.md](docs/STATUS.md).

## Using Fihcore from another project

User programs include `include/fihcore/syscall.h`, link `build/user/crt0.o`
with `user/user.ld`, and are loaded by the kernel from any mounted filesystem.
The kernel command line selects the first program: `init=/system/bin/init`.

## License

MIT, see [LICENSE](LICENSE).
