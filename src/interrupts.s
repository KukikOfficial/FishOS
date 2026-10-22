.section .text

.global gdt_flush
gdt_flush:
    mov 4(%esp), %eax
    lgdt (%eax)
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    ljmp $0x08, $.flush
.flush:
    ret

.global tss_flush
tss_flush:
    mov $0x2B, %ax    /* 0x28 | 3 = 0x2B */
    ltr %ax
    ret

.macro ISR_NOERRCODE num
.global isr\num
isr\num:
    pushl $0
    pushl $\num
    jmp isr_common_stub
.endm

.macro ISR_ERRCODE num
.global isr\num
isr\num:
    pushl $\num
    jmp isr_common_stub
.endm

.macro IRQ num, int_num
.global irq\num
irq\num:
    pushl $0
    pushl $\int_num
    jmp irq_common_stub
.endm

/* CPU Exceptions 0..31 */
ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_ERRCODE   30
ISR_NOERRCODE 31

/* Hardware IRQs 0..15 -> INT 32..47 */
IRQ 0,  32
IRQ 1,  33
IRQ 2,  34
IRQ 3,  35
IRQ 4,  36
IRQ 5,  37
IRQ 6,  38
IRQ 7,  39
IRQ 8,  40
IRQ 9,  41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

/* System Call: INT 0x80 (128) */
ISR_NOERRCODE 128

.extern isr_handler
isr_common_stub:
    pusha
    mov %ds, %ax
    pushl %eax

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    pushl %esp
    call isr_handler
    add $4, %esp

    popl %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    popa
    add $8, %esp
    iret

.extern irq_handler
irq_common_stub:
    pusha
    mov %ds, %ax
    pushl %eax

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    pushl %esp
    call irq_handler
    add $4, %esp

    popl %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    popa
    add $8, %esp
    iret

/* Context switch: context_switch(uint32_t** old_esp, uint32_t new_esp) */
.global context_switch
context_switch:
    push %ebp
    push %ebx
    push %esi
    push %edi

    mov 20(%esp), %eax /* old_esp */
    test %eax, %eax
    jz .load_next
    mov %esp, (%eax)

.load_next:
    mov 24(%esp), %esp /* new_esp */

    pop %edi
    pop %esi
    pop %ebx
    pop %ebp
    ret

/* Enter Ring 3: enter_user_mode(uint32_t entry_point, uint32_t user_stack) */
.global enter_user_mode
enter_user_mode:
    mov 4(%esp), %ebx /* entry_point */
    mov 8(%esp), %ecx /* user_stack */

    cli
    mov $0x23, %ax /* User Data Segment 0x20 | 3 */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    pushl $0x23        /* SS */
    pushl %ecx         /* ESP */
    pushfl             /* EFLAGS */
    popl %eax
    orl $0x200, %eax   /* Enable interrupts (IF bit) */
    pushl %eax
    pushl $0x1B        /* CS: User Code Segment 0x18 | 3 */
    pushl %ebx         /* EIP */
    iret
