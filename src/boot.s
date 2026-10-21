.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set VIDMODE,  1<<2
.set FLAGS,    ALIGN | MEMINFO | VIDMODE
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM
/* AOUT kludge fields (не используются для ELF) */
.long 0, 0, 0, 0, 0
/* Запрос видеорежима: 0 = линейный графический фреймбуфер, 1024x768, 32 bpp */
.long 0
.long 1024
.long 768
.long 32

.section .bss
.align 16
stack_bottom:
.skip 16384 /* 16 KiB стек */
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    mov $stack_top, %esp

    pushl $0
    popf

    /* Передача аргументов: (magic, multiboot_info_ptr) */
    push %ebx
    push %eax

    call kernel_main

    cli
halt_loop:
    hlt
    jmp halt_loop

.size _start, . - _start