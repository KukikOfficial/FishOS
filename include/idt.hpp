#pragma once
#include <stdint.h>

struct idt_entry {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct registers_t {
    uint32_t ds;                                     // Data segment selector
    uint32_t edi, esi, ebp, useless_esp, ebx, edx, ecx, eax; // Pushed by pusha
    uint32_t int_no, err_code;                       // Interrupt number and error code
    uint32_t eip, cs, eflags, useresp, ss;           // Pushed by the processor automatically
};

typedef void (*isr_handler_t)(registers_t* regs);

void idt_init();
void register_interrupt_handler(uint8_t n, isr_handler_t handler);
void pic_send_eoi(uint8_t irq);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
