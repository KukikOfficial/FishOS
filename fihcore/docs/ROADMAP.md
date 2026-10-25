# Fihcore roadmap

Ordered by value to user-space environments built on Fihcore.

1. Threads within a process and a futex-style wait primitive.
2. Per-segment page permissions (W^X) and guard pages below the user stack.
3. Shared libraries or at least a stable static runtime split.
4. `poll`-style waiting on mailboxes and file descriptors together.
5. FihFS: journaling or copy-on-write metadata, larger files (double indirect).
6. AHCI driver; ATA DMA.
7. ACPI table parsing for shutdown and future SMP.
8. User accounts and permission checks on files and process control.
