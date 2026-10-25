/* Link script for Fihcore user programs (static, loaded at 0x40000000). */
ENTRY(_start)

SECTIONS
{
    . = 0x40000000;
    .text : { *(.text*) }
    . = ALIGN(4K);
    .rodata : { *(.rodata*) }
    . = ALIGN(4K);
    .data : { *(.data*) }
    .bss : { *(COMMON) *(.bss*) }
    . = ALIGN(4K);
    _end = .;
    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note*) }
}
