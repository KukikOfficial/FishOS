#include "graphics.hpp"
#include "font.hpp"

static uint32_t backbuffer[SCREEN_W * SCREEN_H];
static uint32_t* vram = nullptr;
static uint32_t pitch_words = SCREEN_W;

void gfx_init(uint32_t* vram_addr, uint32_t pitch_bytes) {
    vram = vram_addr;
    pitch_words = pitch_bytes / 4;
}

void put_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H) {
        backbuffer[y * SCREEN_W + x] = color;
    }
}

void fill_rect(int x, int y, int w, int h, uint32_t color) {
    int x1 = (x < 0) ? 0 : x;
    int y1 = (y < 0) ? 0 : y;
    int x2 = (x + w > SCREEN_W) ? SCREEN_W : x + w;
    int y2 = (y + h > SCREEN_H) ? SCREEN_H : y + h;
    for (int cy = y1; cy < y2; cy++) {
        uint32_t* row = &backbuffer[cy * SCREEN_W];
        for (int cx = x1; cx < x2; cx++) row[cx] = color;
    }
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
    fill_rect(x, y, w, 1, color);
    fill_rect(x, y + h - 1, w, 1, color);
    fill_rect(x, y, 1, h, color);
    fill_rect(x + w - 1, y, 1, h, color);
}

void draw_gradient_v(int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    int r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    int r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    for (int cy = 0; cy < h; cy++) {
        int r = r1 + ((r2 - r1) * cy) / h;
        int g = g1 + ((g2 - g1) * cy) / h;
        int b = b1 + ((b2 - b1) * cy) / h;
        fill_rect(x, y + cy, w, 1, rgb(r, g, b));
    }
}

void draw_shadow(int x, int y, int w, int h) {
    for (int sy = 0; sy < h; sy++) {
        for (int sx = 0; sx < w; sx++) {
            int px = x + sx, py = y + sy;
            if (px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H) {
                uint32_t orig = backbuffer[py * SCREEN_W + px];
                backbuffer[py * SCREEN_W + px] = rgb(((orig >> 16) & 0xFF) / 2, ((orig >> 8) & 0xFF) / 2, (orig & 0xFF) / 2);
            }
        }
    }
}

void draw_char(int x, int y, char c, uint32_t fg) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t* glyph = font8x8[c - 32];
    for (int cy = 0; cy < 8; cy++) {
        uint8_t row = glyph[cy];
        for (int cx = 0; cx < 8; cx++) {
            if ((row >> (7 - cx)) & 1) put_pixel(x + cx, y + cy, fg);
        }
    }
}

void draw_string(int x, int y, const char* str, uint32_t fg) {
    int cur_x = x, cur_y = y;
    while (*str) {
        if (*str == '\n') {
            cur_x = x;
            cur_y += 12;
        } else {
            draw_char(cur_x, cur_y, *str, fg);
            cur_x += 8;
        }
        str++;
    }
}

static const char* cursor_sprite[16] = {
    "X               ",
    "XX              ",
    "X.X             ",
    "X..X            ",
    "X...X           ",
    "X....X          ",
    "X.....X         ",
    "X......X        ",
    "X.......X       ",
    "X.....XXXX      ",
    "X..X..X         ",
    "X.X X..X        ",
    "XX   X..X       ",
    "X     X..X      ",
    "       XX       ",
    "                "
};

void draw_cursor(int mx, int my) {
    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx < 16; cx++) {
            char p = cursor_sprite[cy][cx];
            if (p == 'X') put_pixel(mx + cx, my + cy, 0x000000);
            else if (p == '.') put_pixel(mx + cx, my + cy, 0xFFFFFF);
        }
    }
}

void gfx_present() {
    if (!vram) return;
    for (int y = 0; y < SCREEN_H; y++) {
        uint32_t* dest = vram + y * pitch_words;
        const uint32_t* src = backbuffer + y * SCREEN_W;
        for (int x = 0; x < SCREEN_W; x++) {
            dest[x] = src[x];
        }
    }
}