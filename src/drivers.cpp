#include "drivers.hpp"
#include "io.hpp"
#include "gui.hpp"
#include "idt.hpp"
#include "timer.hpp"

// RTC
static uint8_t rtc_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static uint8_t bcd_to_bin(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

void get_rtc_time(int& h, int& m, int& s) {
    outb(0x70, 0x0A);
    while (inb(0x71) & 0x80);
    uint8_t regb = rtc_read(0x0B);
    s = rtc_read(0x00);
    m = rtc_read(0x02);
    h = rtc_read(0x04);
    if (!(regb & 0x04)) {
        s = bcd_to_bin(s);
        m = bcd_to_bin(m);
        h = bcd_to_bin(h & 0x7F) | (h & 0x80);
    }
}

// ATA IDE PIO
bool ata_present = false;

static void ata_wait_ready() {
    while (inb(0x1F7) & 0x80);
    while (!(inb(0x1F7) & 0x08));
}

void ata_read_sector(uint32_t lba, uint8_t* buffer) {
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, 1);
    outb(0x1F3, (uint8_t)lba);
    outb(0x1F4, (uint8_t)(lba >> 8));
    outb(0x1F5, (uint8_t)(lba >> 16));
    outb(0x1F7, 0x20);
    ata_wait_ready();
    for (int i = 0; i < 256; i++) {
        uint16_t data = inw(0x1F0);
        buffer[i * 2] = data & 0xFF;
        buffer[i * 2 + 1] = (data >> 8) & 0xFF;
    }
}

void ata_write_sector(uint32_t lba, const uint8_t* buffer) {
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, 1);
    outb(0x1F3, (uint8_t)lba);
    outb(0x1F4, (uint8_t)(lba >> 8));
    outb(0x1F5, (uint8_t)(lba >> 16));
    outb(0x1F7, 0x30);
    ata_wait_ready();
    for (int i = 0; i < 256; i++) {
        uint16_t data = buffer[i * 2] | (buffer[i * 2 + 1] << 8);
        outw(0x1F0, data);
    }
    outb(0x1F7, 0xE7);
    while (inb(0x1F7) & 0x80);
}

// Audio Feedback (PC Speaker via PIT Channel 2 & Port 0x61)
void audio_beep(uint32_t freq, uint32_t ms) {
    if (freq == 0) return;
    uint32_t div = 1193180 / freq;
    outb(0x43, 0xB6);
    outb(0x42, static_cast<uint8_t>(div & 0xFF));
    outb(0x42, static_cast<uint8_t>((div >> 8) & 0xFF));
    uint8_t tmp = inb(0x61);
    if ((tmp & 3) != 3) {
        outb(0x61, tmp | 3);
    }
    sleep_ms(ms);
    outb(0x61, inb(0x61) & 0xFC);
}

void audio_click() {
    outb(0x43, 0xB6);
    outb(0x42, 0x80);
    outb(0x42, 0x03); // ~2400 Hz micro-click
    uint8_t tmp = inb(0x61);
    outb(0x61, tmp | 3);
    for (volatile int i = 0; i < 30000; i = i + 1);
    outb(0x61, tmp & 0xFC);
}

// PS/2 Keyboard & Mouse
int mouse_x = 512;
int mouse_y = 384;
bool mouse_btn_left = false;
bool mouse_btn_right = false;
bool shift_pressed = false;
bool ctrl_pressed = false;
bool alt_pressed = false;

static void mouse_wait_write() { uint32_t t = 100000; while (t-- && (inb(0x64) & 2)); }
static void mouse_wait_read()  { uint32_t t = 100000; while (t-- && !(inb(0x64) & 1)); }

static void mouse_write(uint8_t val) {
    mouse_wait_write(); outb(0x64, 0xD4);
    mouse_wait_write(); outb(0x60, val);
}

static uint8_t mouse_read() {
    mouse_wait_read(); return inb(0x60);
}

static char scancode_to_char(uint8_t sc, bool shift) {
    switch (sc) {
        case 0x02: return shift ? '!' : '1';
        case 0x03: return shift ? '@' : '2';
        case 0x04: return shift ? '#' : '3';
        case 0x05: return shift ? '$' : '4';
        case 0x06: return shift ? '%' : '5';
        case 0x07: return shift ? '^' : '6';
        case 0x08: return shift ? '&' : '7';
        case 0x09: return shift ? '*' : '8';
        case 0x0A: return shift ? '(' : '9';
        case 0x0B: return shift ? ')' : '0';
        case 0x0C: return shift ? '_' : '-';
        case 0x0D: return shift ? '+' : '=';
        case 0x10: return shift ? 'Q' : 'q';
        case 0x11: return shift ? 'W' : 'w';
        case 0x12: return shift ? 'E' : 'e';
        case 0x13: return shift ? 'R' : 'r';
        case 0x14: return shift ? 'T' : 't';
        case 0x15: return shift ? 'Y' : 'y';
        case 0x16: return shift ? 'U' : 'u';
        case 0x17: return shift ? 'I' : 'i';
        case 0x18: return shift ? 'O' : 'o';
        case 0x19: return shift ? 'P' : 'p';
        case 0x1E: return shift ? 'A' : 'a';
        case 0x1F: return shift ? 'S' : 's';
        case 0x20: return shift ? 'D' : 'd';
        case 0x21: return shift ? 'F' : 'f';
        case 0x22: return shift ? 'G' : 'g';
        case 0x23: return shift ? 'H' : 'h';
        case 0x24: return shift ? 'J' : 'j';
        case 0x25: return shift ? 'K' : 'k';
        case 0x26: return shift ? 'L' : 'l';
        case 0x2C: return shift ? 'Z' : 'z';
        case 0x2D: return shift ? 'X' : 'x';
        case 0x2E: return shift ? 'C' : 'c';
        case 0x2F: return shift ? 'V' : 'v';
        case 0x30: return shift ? 'B' : 'b';
        case 0x31: return shift ? 'N' : 'n';
        case 0x32: return shift ? 'M' : 'm';
        case 0x39: return ' ';
        case 0x1C: return '\n';
        case 0x33: return shift ? '<' : ',';
        case 0x34: return shift ? '>' : '.';
        case 0x35: return shift ? '?' : '/';
        default: return 0;
    }
}

static int mouse_cycle = 0;
static uint8_t mouse_packet[3];

static void on_keyboard_data(uint8_t data) {
    if (data == 0x2A || data == 0x36) {
        shift_pressed = true;
    } else if (data == 0xAA || data == 0xB6) {
        shift_pressed = false;
    } else if (data == 0x1D) {
        ctrl_pressed = true;
    } else if (data == 0x9D) {
        ctrl_pressed = false;
    } else if (data == 0x38) {
        alt_pressed = true;
    } else if (data == 0xB8) {
        alt_pressed = false;
    } else if (!(data & 0x80)) {
        // Handle Ctrl+S for instant save
        if (ctrl_pressed && data == 0x1F) {
            gui_handle_special_key(KEY_SAVE);
            return;
        }

        // Navigation and special keys
        switch (data) {
            case 0x0E: gui_handle_backspace(); break;
            case 0x4B: gui_handle_special_key(KEY_LEFT); break;
            case 0x4D: gui_handle_special_key(KEY_RIGHT); break;
            case 0x48: gui_handle_special_key(KEY_UP); break;
            case 0x50: gui_handle_special_key(KEY_DOWN); break;
            case 0x47: gui_handle_special_key(KEY_HOME); break;
            case 0x4F: gui_handle_special_key(KEY_END); break;
            case 0x53: gui_handle_special_key(KEY_DELETE); break;
            case 0x0F: gui_handle_special_key(KEY_TAB); break;
            default: {
                char c = scancode_to_char(data, shift_pressed);
                if (c) gui_handle_char(c);
                break;
            }
        }
    }
}

static void on_mouse_data(uint8_t data) {
    switch (mouse_cycle) {
        case 0:
            if (data & 0x08) { mouse_packet[0] = data; mouse_cycle = 1; }
            break;
        case 1: mouse_packet[1] = data; mouse_cycle = 2; break;
        case 2:
            mouse_packet[2] = data; mouse_cycle = 0;
            mouse_btn_left  = (mouse_packet[0] & 1);
            mouse_btn_right = (mouse_packet[0] & 2);

            int dx = mouse_packet[1]; if (mouse_packet[0] & 0x10) dx -= 256;
            int dy = mouse_packet[2]; if (mouse_packet[0] & 0x20) dy -= 256;
            mouse_x += dx; mouse_y -= dy;
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_x >= 1024) mouse_x = 1023;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_y >= 768) mouse_y = 767;
            break;
    }
}

static void keyboard_interrupt_handler(registers_t*) {
    uint8_t st = inb(0x64);
    if ((st & 1) && !(st & 0x20)) {
        uint8_t data = inb(0x60);
        on_keyboard_data(data);
    }
}

static void mouse_interrupt_handler(registers_t*) {
    uint8_t st = inb(0x64);
    if ((st & 1) && (st & 0x20)) {
        uint8_t data = inb(0x60);
        on_mouse_data(data);
    }
}

void mouse_init() {
    register_interrupt_handler(33, keyboard_interrupt_handler); // IRQ1
    register_interrupt_handler(44, mouse_interrupt_handler);    // IRQ12

    mouse_wait_write(); outb(0x64, 0xA8); // Enable auxiliary device
    mouse_wait_write(); outb(0x64, 0x20); // Read Compaq Status
    uint8_t status = mouse_read();
    status |= (1 << 0) | (1 << 1);        // Enable IRQ1 and IRQ12 interrupts!
    status &= ~((1 << 4) | (1 << 5));     // Enable keyboard and mouse clocks
    mouse_wait_write(); outb(0x64, 0x60); // Set Status
    mouse_wait_write(); outb(0x60, status);

    mouse_write(0xF6); mouse_read();     // Set default settings
    mouse_write(0xF4); mouse_read();     // Enable streaming

    // Pleasant startup audio chime (523Hz C5 -> 659Hz E5)
    audio_beep(523, 40);
    audio_beep(659, 60);
}

void process_inputs() {
    while (true) {
        uint8_t st = inb(0x64);
        if (!(st & 1)) break;
        uint8_t data = inb(0x60);
        if (st & 0x20) {
            on_mouse_data(data);
        } else {
            on_keyboard_data(data);
        }
    }
}