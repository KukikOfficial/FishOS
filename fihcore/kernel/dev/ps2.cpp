#include "dev.hpp"
#include "arch/x86/cpu.hpp"
#include "lib/lib.hpp"

namespace {
constexpr uint16_t DATA = 0x60, STATUS = 0x64, CMD = 0x64;

bool wait_write() {
    for (int i = 0; i < 100000; i++) if (!(inb(STATUS) & 2)) return true;
    return false;
}
bool wait_read() {
    for (int i = 0; i < 100000; i++) if (inb(STATUS) & 1) return true;
    return false;
}
void ctl(uint8_t c) { if (wait_write()) outb(CMD, c); }
void put(uint8_t v) { if (wait_write()) outb(DATA, v); }
int get() { return wait_read() ? inb(DATA) : -1; }

// Sends a byte to the mouse and waits for ACK (0xFA).
bool mouse_cmd(uint8_t v) {
    ctl(0xD4);
    put(v);
    return get() == 0xFA;
}

const char keymap[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0};
const char keymap_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ', 0};

bool e0;
uint32_t mods;

void on_key(Regs*) {
    uint8_t sc = inb(DATA);
    if (sc == 0xE0) { e0 = true; return; }
    if (sc == 0xE1) return; // Pause: ignored
    bool pressed = !(sc & 0x80);
    uint32_t code = (sc & 0x7F) | (e0 ? 0x100 : 0);
    e0 = false;

    uint32_t bit = 0;
    switch (code) {
    case FIH_KEY_LSHIFT: case FIH_KEY_RSHIFT: bit = FIH_MOD_SHIFT; break;
    case FIH_KEY_LCTRL: case FIH_KEY_RCTRL: bit = FIH_MOD_CTRL; break;
    case FIH_KEY_LALT: case FIH_KEY_RALT: bit = FIH_MOD_ALT; break;
    case FIH_KEY_LSUPER: case FIH_KEY_RSUPER: bit = FIH_MOD_SUPER; break;
    }
    if (bit) mods = pressed ? (mods | bit) : (mods & ~bit);
    if (code == FIH_KEY_CAPS && pressed) mods ^= FIH_MOD_CAPS;

    uint32_t ch = 0;
    if (code < 128) {
        bool shift = mods & FIH_MOD_SHIFT;
        char base = keymap[code];
        if ((mods & FIH_MOD_CAPS) && base >= 'a' && base <= 'z') shift = !shift;
        ch = (uint8_t)(shift ? keymap_shift[code] : base);
    } else if (code == 0x135) {
        ch = '/'; // keypad slash
    } else if (code == 0x11C) {
        ch = '\n'; // keypad enter
        code = FIH_KEY_ENTER;
    }
    fih_msg_t m{};
    m.type = FIH_MSG_KEY;
    m.a[0] = code;
    m.a[1] = pressed;
    m.a[2] = ch;
    m.a[3] = mods;
    input_post(m);
}

uint8_t packet[4];
int packet_len = 3, packet_pos;

void on_mouse(Regs*) {
    uint8_t st = inb(STATUS);
    if (!(st & 0x20)) return;
    uint8_t b = inb(DATA);
    if (packet_pos == 0 && !(b & 0x08)) return; // resync on the always-one bit
    packet[packet_pos++] = b;
    if (packet_pos < packet_len) return;
    packet_pos = 0;
    if (packet[0] & 0xC0) return; // overflow: drop the packet
    int dx = (int)packet[1] - ((packet[0] << 4) & 0x100);
    int dy = (int)packet[2] - ((packet[0] << 3) & 0x100);
    int wheel = packet_len == 4 ? (int8_t)(packet[3] << 4) >> 4 : 0;
    fih_msg_t m{};
    m.type = FIH_MSG_MOUSE;
    m.a[0] = (uint32_t)dx;
    m.a[1] = (uint32_t)-dy;
    m.a[2] = packet[0] & 7;
    m.a[3] = (uint32_t)wheel;
    input_post(m);
}
}

void ps2_init() {
    ctl(0xAD);
    ctl(0xA7);
    for (int i = 0; i < 32 && (inb(STATUS) & 1); i++) inb(DATA);
    ctl(0x20);
    int cfg = get();
    if (cfg < 0) { klog("ps2: controller not responding\n"); return; }
    cfg |= 0x03;    // IRQ1 + IRQ12
    cfg &= ~0x10;   // keyboard clock on
    cfg |= 0x40;    // keep set-1 translation
    ctl(0x60);
    put((uint8_t)cfg);
    ctl(0xAE);
    ctl(0xA8);

    bool mouse = mouse_cmd(0xF6);
    // IntelliMouse wheel detection: sample rates 200, 100, 80 then read the ID.
    if (mouse && mouse_cmd(0xF3) && mouse_cmd(200) && mouse_cmd(0xF3) && mouse_cmd(100) &&
        mouse_cmd(0xF3) && mouse_cmd(80) && mouse_cmd(0xF2)) {
        if (get() == 3) packet_len = 4;
    }
    if (mouse) mouse = mouse_cmd(0xF4);
    klog("ps2: keyboard ready, mouse %s%s\n", mouse ? "ready" : "missing",
         packet_len == 4 ? " (wheel)" : "");
    irq_install(1, on_key);
    irq_install(12, on_mouse);
    irq_unmask(1);
    irq_unmask(12);
}
