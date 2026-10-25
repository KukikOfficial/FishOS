// Device drivers owned by the kernel.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <fihcore/abi.h>

// COM1 serial port, used for all kernel diagnostics.
void serial_init();
void serial_write(const char* s, size_t n);

// Boot console drawn straight into the framebuffer until a display server
// acquires the screen. Panic output always uses it.
struct Framebuffer {
    uint32_t phys;
    uint32_t virt; // kernel mapping (identity before paging)
    uint32_t width, height, pitch, bpp;
};
extern Framebuffer g_fb;
void console_init();
void console_write(const char* s, size_t n);
void console_disable();
void console_panic_mode();

// Programmable interval timer.
constexpr uint32_t TIMER_HZ = 1000;
void timer_init();
uint32_t timer_ticks(); // milliseconds since boot

// CMOS real-time clock.
void rtc_read(fih_time_t* t);
uint32_t rtc_epoch(); // seconds since 2000-01-01

// PS/2 keyboard and mouse; events go to input_post().
void ps2_init();
void input_post(const fih_msg_t& m);

// ATA PIO disks (LBA28).
struct AtaDisk {
    bool present;
    uint16_t io, ctrl;
    bool slave;
    uint32_t sectors;
    char model[41];
};
constexpr int ATA_MAX = 4;
extern AtaDisk g_ata[ATA_MAX];
void ata_init();
int ata_read(int disk, uint32_t lba, uint32_t count, void* buf);
int ata_write(int disk, uint32_t lba, uint32_t count, const void* buf);
int ata_flush(int disk);

void power_reboot();
int power_shutdown();
