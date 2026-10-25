#include "dev.hpp"
#include "arch/x86/cpu.hpp"

namespace {
uint8_t cmos(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}
uint8_t bcd(uint8_t v, bool is_bcd) { return is_bcd ? (v & 0x0F) + (v >> 4) * 10 : v; }
}

void rtc_read(fih_time_t* t) {
    uint32_t f = irq_save();
    for (int i = 0; i < 100000 && (cmos(0x0A) & 0x80); i++) {} // update in progress
    uint8_t s = cmos(0x00), m = cmos(0x02), h = cmos(0x04), d = cmos(0x07), mo = cmos(0x08),
            y = cmos(0x09), regb = cmos(0x0B);
    irq_restore(f);
    bool is_bcd = !(regb & 0x04);
    bool pm = h & 0x80;
    h &= 0x7F;
    t->second = bcd(s, is_bcd);
    t->minute = bcd(m, is_bcd);
    t->hour = bcd(h, is_bcd);
    if (!(regb & 0x02) && pm) t->hour = (t->hour % 12) + 12; // 12-hour mode
    t->day = bcd(d, is_bcd);
    t->month = bcd(mo, is_bcd);
    t->year = 2000 + bcd(y, is_bcd);
    t->_pad = 0;
}

uint32_t rtc_epoch() {
    fih_time_t t;
    rtc_read(&t);
    static const uint16_t mdays[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    uint32_t y = t.year - 2000;
    uint32_t days = y * 365 + (y + 3) / 4 + mdays[(t.month - 1) % 12] + t.day - 1;
    if (t.month > 2 && (t.year % 4) == 0) days++;
    return ((days * 24 + t.hour) * 60 + t.minute) * 60 + t.second;
}
