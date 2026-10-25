#include "dev.hpp"
#include "arch/x86/cpu.hpp"
#include "proc/proc.hpp"

namespace {
volatile uint32_t ticks;
void on_tick(Regs* r) {
    ticks = ticks + 1;
    sched_tick(r);
}
}

void timer_init() {
    uint32_t div = 1193182 / TIMER_HZ;
    outb(0x43, 0x36);
    outb(0x40, div & 0xFF);
    outb(0x40, div >> 8);
    irq_install(0, on_tick);
    irq_unmask(0);
}

uint32_t timer_ticks() { return ticks; }
