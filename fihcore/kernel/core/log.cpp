#include "lib/lib.hpp"
#include "dev/dev.hpp"
#include "arch/x86/cpu.hpp"

namespace { bool at_line_start = true; }

void klog_write(const char* s, size_t len) {
    uint32_t f = irq_save();
    serial_write(s, len);
    console_write(s, len);
    if (len) at_line_start = s[len - 1] == '\n';
    irq_restore(f);
}

void klog(const char* fmt, ...) {
    char buf[512];
    int n = 0;
    if (at_line_start) {
        uint32_t t = timer_ticks();
        n = snprintf(buf, sizeof(buf), "[%5u.%03u] ", t / 1000, t % 1000);
    }
    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(buf + n, sizeof(buf) - (size_t)n, fmt, ap);
    va_end(ap);
    size_t total = (size_t)n + (size_t)m;
    if (total >= sizeof(buf)) total = sizeof(buf) - 1;
    klog_write(buf, total);
}

namespace {
[[noreturn]] void halt_forever() {
    for (;;) asm volatile("cli; hlt");
}

void dump_regs(const Regs* r) {
    uint32_t cr2;
    asm volatile("mov %%cr2, %0" : "=r"(cr2));
    klog("eax=%p ebx=%p ecx=%p edx=%p\n", r->eax, r->ebx, r->ecx, r->edx);
    klog("esi=%p edi=%p ebp=%p eip=%p\n", r->esi, r->edi, r->ebp, r->eip);
    klog("cs=%x eflags=%p cr2=%p err=%x\n", r->cs, r->eflags, cr2, r->err);
}

void backtrace(uint32_t ebp) {
    klog("backtrace:\n");
    for (int i = 0; i < 12 && ebp >= 0x100000 && ebp < 0x40000000 && !(ebp & 3); i++) {
        uint32_t* frame = reinterpret_cast<uint32_t*>(ebp);
        klog("  %p\n", frame[1]);
        ebp = frame[0];
    }
}

void panic_begin(const char* fmt, va_list ap) {
    cli();
    console_panic_mode();
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    klog("\n*** FIHCORE KERNEL PANIC ***\n%s\n", buf);
}
}

void panic(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    panic_begin(fmt, ap);
    va_end(ap);
    uint32_t ebp;
    asm volatile("mov %%ebp, %0" : "=r"(ebp));
    backtrace(ebp);
    klog("System halted.\n");
    halt_forever();
}

void panic_regs(const Regs* r, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    panic_begin(fmt, ap);
    va_end(ap);
    dump_regs(r);
    backtrace(r->ebp);
    klog("System halted.\n");
    halt_forever();
}
