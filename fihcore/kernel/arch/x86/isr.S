/* Interrupt entry stubs. Every vector builds the Regs frame from cpu.hpp. */
.macro ISR_NOERR n
.global isr\n
isr\n:
    pushl $0
    pushl $\n
    jmp isr_common
.endm

.macro ISR_ERR n
.global isr\n
isr\n:
    pushl $\n
    jmp isr_common
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31
.irp n, 32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
ISR_NOERR \n
.endr
ISR_NOERR 128

isr_common:
    pusha
    push %ds
    push %es
    push %fs
    push %gs
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    cld
    push %esp
    call isr_dispatch
    add $4, %esp
.global isr_return
isr_return:
    pop %gs
    pop %fs
    pop %es
    pop %ds
    popa
    add $8, %esp
    iret

.section .rodata
.global isr_table
isr_table:
.irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
.long isr\n
.endr

.section .text
/* void context_switch(uint32_t* save_esp, uint32_t new_esp, uint32_t new_cr3)
 * Saves callee-saved registers and EFLAGS on the current kernel stack and
 * resumes another kernel stack previously saved the same way. */
.global context_switch
context_switch:
    push %ebp
    push %ebx
    push %esi
    push %edi
    pushf
    mov 24(%esp), %eax
    mov %esp, (%eax)
    mov 28(%esp), %edx
    mov 32(%esp), %ecx
    mov %cr3, %eax
    cmp %eax, %ecx
    je 1f
    mov %ecx, %cr3
1:  mov %edx, %esp
    popf
    pop %edi
    pop %esi
    pop %ebx
    pop %ebp
    ret
