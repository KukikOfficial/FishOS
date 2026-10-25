#include "cpu.hpp"
#include "lib/lib.hpp"

namespace {
struct [[gnu::packed]] Entry {
    uint16_t limit_lo, base_lo;
    uint8_t base_mid, access, gran, base_hi;
};
struct [[gnu::packed]] Ptr {
    uint16_t limit;
    uint32_t base;
};
struct [[gnu::packed]] Tss {
    uint32_t prev, esp0, ss0, esp1, ss1, esp2, ss2, cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap, iomap;
};

Entry gdt[6];
Tss tss;

void set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i] = {uint16_t(limit & 0xFFFF), uint16_t(base & 0xFFFF), uint8_t(base >> 16), access,
              uint8_t(((limit >> 16) & 0x0F) | (gran & 0xF0)), uint8_t(base >> 24)};
}
}

void gdt_init() {
    set(0, 0, 0, 0, 0);
    set(1, 0, 0xFFFFF, 0x9A, 0xC0); // kernel code
    set(2, 0, 0xFFFFF, 0x92, 0xC0); // kernel data
    set(3, 0, 0xFFFFF, 0xFA, 0xC0); // user code
    set(4, 0, 0xFFFFF, 0xF2, 0xC0); // user data
    memset(&tss, 0, sizeof(tss));
    tss.ss0 = SEL_KDATA;
    tss.iomap = sizeof(Tss); // no I/O permission bitmap: ring 3 port access faults
    set(5, (uint32_t)&tss, sizeof(Tss) - 1, 0x89, 0x00);

    Ptr p{sizeof(gdt) - 1, (uint32_t)&gdt};
    asm volatile(
        "lgdt %0\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n mov %%ax, %%es\n mov %%ax, %%fs\n mov %%ax, %%gs\n mov %%ax, %%ss\n"
        "ljmp $0x08, $1f\n"
        "1:\n"
        "mov $0x28, %%ax\n"
        "ltr %%ax\n"
        : : "m"(p) : "eax", "memory");
}

void gdt_set_kernel_stack(uint32_t esp0) { tss.esp0 = esp0; }
