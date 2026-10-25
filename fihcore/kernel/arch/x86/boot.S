/* Multiboot 1 entry point. GRUB loads us at 1 MiB in 32-bit protected mode. */
.set MB_ALIGN,   1 << 0
.set MB_MEMINFO, 1 << 1
.set MB_VIDEO,   1 << 2
.set MB_FLAGS,   MB_ALIGN | MB_MEMINFO | MB_VIDEO
.set MB_MAGIC,   0x1BADB002

.section .multiboot, "a"
.align 4
.long MB_MAGIC
.long MB_FLAGS
.long -(MB_MAGIC + MB_FLAGS)
.long 0, 0, 0, 0, 0       /* a.out fields, unused for ELF */
.long 0                   /* preferred mode: linear framebuffer */
.long 1024, 768, 32       /* preference only; the kernel uses whatever GRUB sets */

.section .bss
.align 16
.global boot_stack_bottom
boot_stack_bottom:
.skip 32768
.global boot_stack_top
boot_stack_top:

.section .text
.global _start
.type _start, @function
_start:
    mov $boot_stack_top, %esp
    xor %ebp, %ebp
    pushl $0
    popf
    push %ebx               /* multiboot info */
    push %eax               /* magic */
    call kernel_main
1:  cli
    hlt
    jmp 1b
