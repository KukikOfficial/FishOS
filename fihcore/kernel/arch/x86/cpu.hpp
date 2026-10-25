// x86 port I/O, control registers and the interrupt register frame.
#pragma once
#include <stdint.h>

static inline void outb(uint16_t p, uint8_t v) { asm volatile("outb %0, %1" : : "a"(v), "Nd"(p)); }
static inline void outw(uint16_t p, uint16_t v) { asm volatile("outw %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t v; asm volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline uint16_t inw(uint16_t p) { uint16_t v; asm volatile("inw %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void io_wait() { outb(0x80, 0); }

static inline void cli() { asm volatile("cli" ::: "memory"); }
static inline void sti() { asm volatile("sti" ::: "memory"); }
static inline void hlt() { asm volatile("hlt"); }

static inline uint32_t irq_save() {
    uint32_t f;
    asm volatile("pushfl; popl %0; cli" : "=r"(f) : : "memory");
    return f;
}
static inline void irq_restore(uint32_t f) {
    if (f & 0x200) sti();
}

static inline uint32_t read_cr2() { uint32_t v; asm volatile("mov %%cr2, %0" : "=r"(v)); return v; }
static inline uint32_t read_cr3() { uint32_t v; asm volatile("mov %%cr3, %0" : "=r"(v)); return v; }
static inline void write_cr3(uint32_t v) { asm volatile("mov %0, %%cr3" : : "r"(v) : "memory"); }
static inline void invlpg(uint32_t va) { asm volatile("invlpg (%0)" : : "r"(va) : "memory"); }

// Layout pushed by isr.S; must stay in sync with it.
struct Regs {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_unused, ebx, edx, ecx, eax;
    uint32_t int_no, err;
    uint32_t eip, cs, eflags, user_esp, user_ss; // last two only valid from ring 3
};

static inline bool regs_from_user(const Regs* r) { return (r->cs & 3) == 3; }

enum : uint16_t {
    SEL_KCODE = 0x08,
    SEL_KDATA = 0x10,
    SEL_UCODE = 0x1B,
    SEL_UDATA = 0x23,
    SEL_TSS = 0x28,
};

void gdt_init();
void gdt_set_kernel_stack(uint32_t esp0);
void idt_init();
using IrqHandler = void (*)(Regs*);
void irq_install(int irq, IrqHandler h);
void irq_unmask(int irq);
