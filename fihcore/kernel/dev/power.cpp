#include "dev.hpp"
#include "arch/x86/cpu.hpp"
#include "lib/lib.hpp"

void power_reboot() {
    cli();
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) {}
    outb(0x64, 0xFE); // keyboard controller reset line
    struct [[gnu::packed]] { uint16_t limit; uint32_t base; } null_idt{0, 0};
    asm volatile("lidt %0; int $3" : : "m"(null_idt)); // triple fault fallback
    for (;;) hlt();
}

int power_shutdown() {
    // Without an ACPI interpreter only the emulator-specific ports work.
    outw(0x604, 0x2000);  // QEMU (PIIX4/Q35 PM1a control)
    outw(0xB004, 0x2000); // Bochs and older QEMU
    outw(0x4004, 0x3400); // VirtualBox
    return -FIH_ENOSYS;
}
