#pragma once
#include <stdint.h>

constexpr int SCREEN_W = 1024;
constexpr int SCREEN_H = 768;

static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

void gfx_init(uint32_t* vram_addr, uint32_t pitch_bytes);
void gfx_present();

void put_pixel(int x, int y, uint32_t color);
void fill_rect(int x, int y, int w, int h, uint32_t color);
void draw_rect(int x, int y, int w, int h, uint32_t color);
void draw_gradient_v(int x, int y, int w, int h, uint32_t c1, uint32_t c2);
void draw_shadow(int x, int y, int w, int h);
void draw_char(int x, int y, char c, uint32_t fg);
void draw_string(int x, int y, const char* str, uint32_t fg);
void draw_cursor(int mx, int my);