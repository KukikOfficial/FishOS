#include "dev.hpp"
#include "font8x8.hpp"
#include "lib/lib.hpp"

Framebuffer g_fb;

namespace {
bool enabled;
uint32_t cols, rows, cx, cy;
uint32_t fg = 0xC8D3DC, bg = 0x0B1620;
constexpr uint32_t CW = 8, CH = 10;

uint32_t* row_ptr(uint32_t y) { return (uint32_t*)(g_fb.virt + y * g_fb.pitch); }

void draw_char(uint32_t col, uint32_t row, char c) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t* g = font8x8[c - 32];
    for (uint32_t y = 0; y < CH; y++) {
        uint32_t* p = row_ptr(row * CH + y) + col * CW;
        uint8_t bits = y < 8 ? g[y] : 0;
        for (uint32_t x = 0; x < CW; x++) p[x] = (bits >> (7 - x)) & 1 ? fg : bg;
    }
}

void scroll() {
    uint32_t line = g_fb.pitch * CH;
    memmove((void*)g_fb.virt, (void*)(g_fb.virt + line), line * (rows - 1));
    for (uint32_t y = (rows - 1) * CH; y < rows * CH; y++) {
        uint32_t* p = row_ptr(y);
        for (uint32_t x = 0; x < g_fb.width; x++) p[x] = bg;
    }
    cy = rows - 1;
}
}

void console_init() {
    if (!g_fb.virt || g_fb.bpp != 32) return;
    cols = g_fb.width / CW;
    rows = g_fb.height / CH;
    for (uint32_t y = 0; y < g_fb.height; y++) {
        uint32_t* p = row_ptr(y);
        for (uint32_t x = 0; x < g_fb.width; x++) p[x] = bg;
    }
    enabled = true;
}

void console_write(const char* s, size_t n) {
    if (!enabled) return;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\n') { cx = 0; if (++cy >= rows) scroll(); continue; }
        if (c == '\r') { cx = 0; continue; }
        draw_char(cx, cy, c);
        if (++cx >= cols) { cx = 0; if (++cy >= rows) scroll(); }
    }
}

void console_disable() { enabled = false; }

void console_panic_mode() {
    if (!g_fb.virt || g_fb.bpp != 32) return;
    bg = 0x5A0E14;
    fg = 0xFFFFFF;
    cols = g_fb.width / CW;
    rows = g_fb.height / CH;
    for (uint32_t y = 0; y < g_fb.height; y++) {
        uint32_t* p = row_ptr(y);
        for (uint32_t x = 0; x < g_fb.width; x++) p[x] = bg;
    }
    cx = cy = 0;
    enabled = true;
}
