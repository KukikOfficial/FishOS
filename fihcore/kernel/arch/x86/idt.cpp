#include "cpu.hpp"
#include "lib/lib.hpp"
#include "proc/proc.hpp"
#include "mm/vmm.hpp"

extern "C" uint32_t isr_table[48];
extern "C" void isr128();

namespace {
struct [[gnu::packed]] Gate {
    uint16_t off_lo, sel;
    uint8_t zero, flags;
    uint16_t off_hi;
};
struct [[gnu::packed]] Ptr {
    uint16_t limit;
    uint32_t base;
};

Gate idt[256];
IrqHandler irq_handlers[16];

const char* const exc_names[32] = {
    "Divide error", "Debug", "NMI", "Breakpoint", "Overflow", "Bound range", "Invalid opcode",
    "Device not available", "Double fault", "Coprocessor overrun", "Invalid TSS",
    "Segment not present", "Stack fault", "General protection fault", "Page fault", "Reserved",
    "x87 FPU error", "Alignment check", "Machine check", "SIMD exception", "Virtualization",
    "Control protection", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Security exception", "Reserved"};

void set_gate(int n, uint32_t handler, uint8_t flags) {
    idt[n] = {uint16_t(handler & 0xFFFF), SEL_KCODE, 0, flags, uint16_t(handler >> 16)};
}

void pic_remap() {
    uint8_t m1 = 0xFF, m2 = 0xFF; // start fully masked; drivers unmask their lines
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait();
    outb(0xA1, 0x28); io_wait();
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();
    outb(0x21, m1 & ~0x04); // keep the cascade line open
    outb(0xA1, m2);
}

void exception(Regs* r) {
    const char* name = exc_names[r->int_no];
    if (r->int_no == 14) {
        uint32_t addr = read_cr2();
        if (regs_from_user(r) && vmm_handle_user_fault(addr, r->err)) return;
        if (regs_from_user(r)) {
            Proc* p = proc_current();
            klog("fault: pid %u (%s) page fault at %p (%s %s) eip=%p, terminated\n", p->pid, p->name,
                 addr, (r->err & 1) ? "protection" : "not-present", (r->err & 2) ? "write" : "read",
                 r->eip);
            proc_exit_current(-128 - 14);
        }
        panic_regs(r, "page fault at %p err=%x", addr, r->err);
    }
    if (regs_from_user(r)) {
        Proc* p = proc_current();
        klog("fault: pid %u (%s) %s (vector %u, err %x) eip=%p, terminated\n", p->pid, p->name, name,
             r->int_no, r->err, r->eip);
        proc_exit_current(-128 - (int)r->int_no);
    }
    panic_regs(r, "%s (vector %u, err %x)", name, r->int_no, r->err);
}
}

void idt_init() {
    pic_remap();
    for (int i = 0; i < 48; i++) set_gate(i, isr_table[i], 0x8E);
    set_gate(0x80, (uint32_t)isr128, 0xEE); // DPL 3 interrupt gate for system calls
    Ptr p{sizeof(idt) - 1, (uint32_t)&idt};
    asm volatile("lidt %0" : : "m"(p));
}

void irq_install(int irq, IrqHandler h) { irq_handlers[irq] = h; }

void irq_unmask(int irq) {
    uint16_t port = irq < 8 ? 0x21 : 0xA1;
    outb(port, inb(port) & ~(1 << (irq & 7)));
}

extern "C" void isr_dispatch(Regs* r) {
    if (r->int_no < 32) {
        exception(r);
    } else if (r->int_no < 48) {
        int irq = r->int_no - 32;
        if (irq == 7 || irq == 15) { // spurious interrupts have no ISR bit set
            uint8_t ocw3 = 0x0B;
            uint16_t cmd = irq == 7 ? 0x20 : 0xA0;
            outb(cmd, ocw3);
            if (!(inb(cmd) & 0x80)) {
                if (irq == 15) outb(0x20, 0x20);
                return;
            }
        }
        if (irq >= 8) outb(0xA0, 0x20);
        outb(0x20, 0x20);
        if (irq_handlers[irq]) irq_handlers[irq](r);
    } else if (r->int_no == 0x80) {
        syscall_dispatch(r);
    }
    if (regs_from_user(r)) sched_return_to_user(r);
}
