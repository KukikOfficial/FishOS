// ATA PIO driver. Polling only, every wait is bounded by a timeout, and every
// command checks ERR/DF so a failing disk reports -FIH_EIO instead of hanging.
#include "dev.hpp"
#include "arch/x86/cpu.hpp"
#include "lib/lib.hpp"

AtaDisk g_ata[ATA_MAX];

namespace {
constexpr uint8_t ST_ERR = 0x01, ST_DRQ = 0x08, ST_DF = 0x20, ST_BSY = 0x80;
constexpr uint32_t TIMEOUT_MS = 2000;

uint8_t alt_status(const AtaDisk& d) { return inb(d.ctrl); }
void delay400(const AtaDisk& d) { for (int i = 0; i < 4; i++) alt_status(d); }

// Waits for BSY to clear (and DRQ to set if want_drq). Uses the timer, which
// keeps running because kernel code executes with interrupts enabled.
int wait(const AtaDisk& d, bool want_drq) {
    uint32_t start = timer_ticks();
    for (uint32_t spins = 0;; spins++) {
        uint8_t s = inb(d.io + 7);
        if (!(s & ST_BSY)) {
            if (s & (ST_ERR | ST_DF)) return -FIH_EIO;
            if (!want_drq || (s & ST_DRQ)) return 0;
        }
        if (timer_ticks() - start > TIMEOUT_MS) return -FIH_EIO;
        if (spins > 50000000) return -FIH_EIO; // timer not running yet
    }
}

void select(const AtaDisk& d, uint32_t lba) {
    outb(d.io + 6, 0xE0 | (d.slave ? 0x10 : 0) | ((lba >> 24) & 0x0F));
    delay400(d);
}

void identify(AtaDisk& d) {
    if (inb(d.io + 7) == 0xFF) return; // floating bus, no controller
    outb(d.ctrl, 0x02);                // nIEN: we poll
    outb(d.io + 6, d.slave ? 0xB0 : 0xA0);
    delay400(d);
    outb(d.io + 2, 0);
    outb(d.io + 3, 0);
    outb(d.io + 4, 0);
    outb(d.io + 5, 0);
    outb(d.io + 7, 0xEC);
    if (inb(d.io + 7) == 0) return;
    uint32_t start = timer_ticks();
    while (inb(d.io + 7) & ST_BSY)
        if (timer_ticks() - start > 500) return;
    if (inb(d.io + 4) || inb(d.io + 5)) return; // ATAPI/SATA signature: not an ATA disk
    if (wait(d, true) < 0) return;
    uint16_t id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(d.io);
    if (!(id[49] & 0x200)) return; // LBA required
    d.sectors = id[60] | ((uint32_t)id[61] << 16);
    for (int i = 0; i < 20; i++) {
        d.model[i * 2] = (char)(id[27 + i] >> 8);
        d.model[i * 2 + 1] = (char)(id[27 + i] & 0xFF);
    }
    d.model[40] = 0;
    for (int i = 39; i >= 0 && d.model[i] == ' '; i--) d.model[i] = 0;
    d.present = d.sectors > 0;
}

int check(int disk, uint32_t lba, uint32_t count) {
    if (disk < 0 || disk >= ATA_MAX || !g_ata[disk].present) return -FIH_ENODEV;
    if (count == 0 || count > 256) return -FIH_EINVAL;
    if (lba >= g_ata[disk].sectors || count > g_ata[disk].sectors - lba) return -FIH_EINVAL;
    if (lba + count > (1u << 28)) return -FIH_EINVAL;
    return 0;
}

void setup(const AtaDisk& d, uint32_t lba, uint32_t count, uint8_t cmd) {
    select(d, lba);
    outb(d.io + 2, count == 256 ? 0 : (uint8_t)count);
    outb(d.io + 3, lba & 0xFF);
    outb(d.io + 4, (lba >> 8) & 0xFF);
    outb(d.io + 5, (lba >> 16) & 0xFF);
    outb(d.io + 7, cmd);
}
}

void ata_init() {
    const uint16_t bases[2][2] = {{0x1F0, 0x3F6}, {0x170, 0x376}};
    for (int i = 0; i < ATA_MAX; i++) {
        AtaDisk& d = g_ata[i];
        d = {};
        d.io = bases[i / 2][0];
        d.ctrl = bases[i / 2][1];
        d.slave = i & 1;
        identify(d);
        if (d.present)
            klog("ata%d: %s, %u sectors (%u MiB)\n", i, d.model, d.sectors, d.sectors / 2048);
    }
}

int ata_read(int disk, uint32_t lba, uint32_t count, void* buf) {
    int e = check(disk, lba, count);
    if (e) return e;
    const AtaDisk& d = g_ata[disk];
    if ((e = wait(d, false)) < 0) return e;
    setup(d, lba, count, 0x20);
    auto* p = static_cast<uint16_t*>(buf);
    for (uint32_t s = 0; s < count; s++) {
        delay400(d);
        if ((e = wait(d, true)) < 0) {
            klog("ata%d: read error lba %u status %x\n", disk, lba + s, inb(d.io + 7));
            return e;
        }
        for (int i = 0; i < 256; i++) *p++ = inw(d.io);
    }
    return 0;
}

int ata_write(int disk, uint32_t lba, uint32_t count, const void* buf) {
    int e = check(disk, lba, count);
    if (e) return e;
    const AtaDisk& d = g_ata[disk];
    if ((e = wait(d, false)) < 0) return e;
    setup(d, lba, count, 0x30);
    auto* p = static_cast<const uint16_t*>(buf);
    for (uint32_t s = 0; s < count; s++) {
        delay400(d);
        if ((e = wait(d, true)) < 0) {
            klog("ata%d: write error lba %u status %x\n", disk, lba + s, inb(d.io + 7));
            return e;
        }
        for (int i = 0; i < 256; i++) outw(d.io, *p++);
    }
    delay400(d);
    if ((e = wait(d, false)) < 0) return e;
    return ata_flush(disk);
}

int ata_flush(int disk) {
    if (disk < 0 || disk >= ATA_MAX || !g_ata[disk].present) return -FIH_ENODEV;
    const AtaDisk& d = g_ata[disk];
    select(d, 0);
    outb(d.io + 7, 0xE7);
    delay400(d);
    return wait(d, false);
}
