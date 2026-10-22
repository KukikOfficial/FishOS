#include "gdt.hpp"
#include "string.hpp"

#define GDT_ENTRIES 6

static gdt_entry gdt[GDT_ENTRIES];
static gdt_ptr   gp;
static tss_entry tss;

extern "C" void gdt_flush(uint32_t);
extern "C" void tss_flush();

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low    = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;

    gdt[num].limit_low   = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[num].access      = access;
}

static void write_tss(int num, uint16_t ss0, uint32_t esp0) {
    uint32_t base = reinterpret_cast<uint32_t>(&tss);
    uint32_t limit = sizeof(tss_entry);

    gdt_set_gate(num, base, limit, 0xE9, 0x00); // 0xE9 = Present, Ring 3, 32-bit TSS

    memset(&tss, 0, sizeof(tss));
    tss.ss0 = ss0;
    tss.esp0 = esp0;
    tss.cs = 0x08 | 0x3;
    tss.ss = 0x10 | 0x3;
    tss.ds = 0x10 | 0x3;
    tss.es = 0x10 | 0x3;
    tss.fs = 0x10 | 0x3;
    tss.gs = 0x10 | 0x3;
}

void set_kernel_stack(uint32_t stack) {
    tss.esp0 = stack;
}

void gdt_init() {
    gp.limit = (sizeof(gdt_entry) * GDT_ENTRIES) - 1;
    gp.base  = reinterpret_cast<uint32_t>(&gdt);

    // 0: Null descriptor
    gdt_set_gate(0, 0, 0, 0, 0);

    // 1: Kernel Code Segment (0x08)
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    // 2: Kernel Data Segment (0x10)
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    // 3: User Code Segment (0x18 | 3 = 0x1B)
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);

    // 4: User Data Segment (0x20 | 3 = 0x23)
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    // 5: Task State Segment (0x28)
    write_tss(5, 0x10, 0);

    gdt_flush(reinterpret_cast<uint32_t>(&gp));
    tss_flush();
}
