# Fihcore architecture

Fihcore is a monolithic, uniprocessor i386 kernel. User programs run in ring 3
with private address spaces and talk to the kernel only through `int $0x80`.

## Source layout

| Path | Contents |
|------|----------|
| `include/fihcore/abi.h` | The ABI: syscall numbers, error codes, shared structures. Single source of truth. |
| `include/fihcore/syscall.h` | Header-only user-space wrappers for every system call. |
| `kernel/arch/x86` | Multiboot entry, GDT/TSS, IDT, PIC, interrupt stubs, context switch. |
| `kernel/core` | `kernel_main`, kernel log, panic. |
| `kernel/mm` | Physical frames (`pmm`), paging (`vmm`), kernel heap. |
| `kernel/dev` | Serial, boot console, PIT, RTC, PS/2, ATA PIO, power. |
| `kernel/fs` | VFS and mounts, FihFS, tarfs (boot image), pipes. |
| `kernel/proc` | Processes, scheduler, ELF loader, IPC, system calls. |
| `user/` | `crt0.c` and `user.ld` for user programs. |
| `tools/`, `tests/` | Host FihFS tool, host unit tests, QEMU integration test. |

## Boot sequence

GRUB loads `fihcore.elf` at 1 MiB with the boot image (a ustar archive) as the
first module. `kernel_main` then: serial → GDT → IDT/PIC → frame allocator
(from the Multiboot memory map) → paging → framebuffer mapping and boot console
→ heap → PIT (1 kHz) → scheduler → PS/2 → ATA probing → VFS mounts → spawns
the program named by `init=` on the kernel command line (default
`/system/bin/init`). The boot CPU then becomes the idle task (`sti; hlt`).

## Memory

Physical memory below 1 GiB is identity-mapped for the kernel with 4 MiB pages,
supervisor-only; page 0 is unmapped so null dereferences fault. MMIO (the
framebuffer) is mapped at `0xE0000000+`. Each process has its own page
directory sharing those kernel entries.

| Range | Use |
|-------|-----|
| `0x00000000-0x3FFFFFFF` | kernel identity map (supervisor) |
| `0x40000000-0x6FFFFFFF` | program image and `brk` heap |
| `0x80000000-0xAFFFFFFF` | shared memory and framebuffer mappings |
| `0xBFBFF000-0xBFFFF000` | user stack, 4 MiB, grows on demand |
| `0xE0000000-` | kernel MMIO |

The heap is a boundary-tag first-fit allocator over one contiguous arena
(4–16 MiB depending on RAM); headers and footers are validated on every
operation and corruption panics with the address.

## Execution model

- Kernel code is not preemptible. Interrupt handlers never switch tasks in
  kernel mode; preemption happens only on the return path to ring 3
  (`sched_return_to_user`). System calls run with interrupts enabled so the
  timer keeps running during ATA polling.
- Blocking uses `proc_block(obj, timeout)` with interrupts disabled around the
  condition check, and `proc_wake(obj)` from the producer (pipes, mailboxes,
  child exit, timers).
- Round robin with a 10 ms quantum. x87 state is saved with `fnsave`/`frstor`
  on every switch; the kernel itself is built with `-mgeneral-regs-only`.
- A fault in ring 3 terminates that process (exit code `-128 - vector`) with a
  log line; a fault in ring 0 is a panic with registers and a backtrace.

## Processes

`spawn(path, argv, fds[3])` loads a static ELF (`ET_EXEC`, i386, segments
inside the image range), builds `argc/argv` on a fresh stack and inherits the
working directory and the three given descriptors. Exited processes become
zombies until the parent calls `wait`; the parent also receives a
`FIH_MSG_CHILD_EXIT` message. Orphans are reaped automatically.

## IPC

- **Mailboxes**: every process has a 128-entry queue of 64-byte `fih_msg_t`.
  `msg_send` never blocks (`-FIH_EAGAIN` when full); `msg_recv` takes a timeout.
  The kernel stamps the sender pid; message types below `FIH_MSG_USER` are
  reserved for the kernel (key, mouse, child exit).
- **Shared memory**: `shm_create` returns an id owned by the caller; other
  processes may map it only after `shm_grant`. Frames are freed when the owner
  destroys it (or exits) and no mapping remains.
- **Names**: `name_register("svc")` / `name_lookup("svc")` lets clients find
  services without hard-coded pids.
- **Display**: `display_acquire` gives one process the framebuffer (mapped into
  its address space) and routes keyboard and mouse events to its mailbox. On
  exit the boot console takes the screen back.

## Storage

- ATA PIO, LBA28, polling with a 2 s timeout per wait, ERR/DF checked on every
  sector, cache flush after writes. Devices answering IDENTIFY as ATAPI are skipped.
- The kernel never formats or writes a disk that does not already contain a
  valid FihFS superblock (magic + checksum + layout validation).
- FihFS (`kernel/fs/fihfs.hpp` documents the on-disk format): 4 KiB blocks,
  bitmap allocation, 128-byte inodes with 12 direct + 1 indirect block
  (max file ~4 MiB), 64-byte directory entries, names up to 57 bytes.
  Every pointer read from disk is range-checked. Update ordering is chosen so a
  crash leaks space instead of creating dangling references; there is no
  journal. `rename` with `FIH_RENAME_REPLACE` swaps the directory entry in a
  single block write, which editors use for safe saves.
- A 64-block write-through cache sits between FihFS and ATA.
- Mounts: `/` is the first ATA disk with FihFS (or an empty read-only root),
  `/system` is the read-only boot image. Paths are normalized in the kernel
  (`.`/`..`, relative to the per-process cwd) before lookup.

## System calls

See `include/fihcore/abi.h` for the authoritative list and argument order.
Every pointer argument is validated against the caller's page tables before
use (`vmm_user_ok`); strings are copied page by page. Errors are returned as
negative `FIH_E*` values.
