#include "dev.hpp"
#include "arch/x86/cpu.hpp"

namespace { constexpr uint16_t COM1 = 0x3F8; bool ready; }

void serial_init() {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01); // 115200 baud
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); // 8N1
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x03);
    ready = inb(COM1 + 5) != 0xFF; // floating bus: no UART
}

void serial_write(const char* s, size_t n) {
    if (!ready) return;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\n') {
            for (int t = 0; t < 100000 && !(inb(COM1 + 5) & 0x20); t++) {}
            outb(COM1, '\r');
        }
        for (int t = 0; t < 100000 && !(inb(COM1 + 5) & 0x20); t++) {}
        outb(COM1, (uint8_t)s[i]);
    }
}
