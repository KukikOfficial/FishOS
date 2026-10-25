# Fihcore status

Legend: **verified** = exercised by `make test` / `make test-host`;
**implemented** = in use but without an automated check.

| Area | State | Notes |
|------|-------|-------|
| Multiboot boot, GDT/TSS, IDT | verified | |
| Exceptions: user fault kills process | verified | null write in a child → exit -142 |
| Kernel panic screen + serial dump | implemented | registers and backtrace |
| Frame allocator from memory map | verified | |
| Paging, per-process address spaces | verified | |
| Demand-grown user stack | verified | 1.5 MiB recursion |
| `brk` heap | verified | |
| Kernel heap with corruption checks | implemented | |
| Preemptive scheduler, sleep, kill | verified | |
| x87 state across switches | verified | |
| ELF loading, spawn/wait/exit | verified | bad paths / non-ELF rejected |
| Pointer validation in syscalls | verified | EFAULT cases |
| Pipes (blocking, non-blocking, EPIPE) | verified | |
| Mailboxes, timeouts, forged-type rejection | verified | |
| Shared memory with grants, size query | verified | ungranted map denied |
| Name registry | verified | |
| Display ownership + input routing | implemented | exercised by FishOS |
| PS/2 keyboard / mouse (wheel) | implemented | exercised by FishOS |
| ATA PIO with timeouts | verified | |
| FihFS | verified | 188 host checks + in-kernel test, fsck clean |
| Persistence across reboot | verified | second boot check |
| tarfs boot image (read-only) | verified | EROFS checked |
| Reboot / shutdown | implemented | shutdown via QEMU/Bochs/VirtualBox ports only |

## Known limitations

- Uniprocessor only; no kernel preemption.
- One thread per process.
- No users/permissions: any process may kill any other.
- Program segments are mapped read-write (no W^X yet).
- FihFS has no journal; max file size ~4 MiB; no hard links or symlinks.
- ATA PIO only (no DMA, no AHCI); LBA28 (128 GiB max).
- No ACPI interpreter: shutdown only works on emulators.
- Kernel heap is a fixed arena.
