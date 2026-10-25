// FishOS 2D drawing: rectangles, alpha text and blits on 32-bit XRGB buffers.
#pragma once
#include <stdint.h>

namespace gfx {

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool empty() const { return w <= 0 || h <= 0; }
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    Rect intersect(const Rect& o) const;
    Rect unite(const Rect& o) const;
    Rect inset(int d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
};

struct Glyph {
    uint8_t advance;
    int8_t xoff, yoff; // offset of the bitmap from the pen position on the baseline
    uint8_t w, h;
    uint16_t offset;
};

struct Font {
    uint8_t height, baseline;
    Glyph glyphs[95]; // ASCII 32..126
    const uint8_t* alpha;
};

extern const Font font_mono;  // FishMono 13 px, 8 px advance
extern const Font font_sans;  // FishSans 14 px
extern const Font font_title; // FishSans 22 px

int text_width(const Font& f, const char* s, int len = -1);
// Number of leading bytes of `s` that fit into `max_w` pixels.
int text_fit(const Font& f, const char* s, int max_w);
uint32_t mix(uint32_t a, uint32_t b, uint32_t t); // t = 0..255, weight of b

struct Canvas {
    uint32_t* px = nullptr;
    int w = 0, h = 0;
    int stride = 0; // pixels per row
    Rect clip;

    void reset(uint32_t* p, int width, int height, int stride_px);
    void fill(Rect r, uint32_t c);
    void fill_rounded(Rect r, int radius, uint32_t c);
    void blend(Rect r, uint32_t c, uint32_t alpha);
    void frame(Rect r, uint32_t c);
    void hline(int x, int y, int w, uint32_t c) { fill({x, y, w, 1}, c); }
    void vline(int x, int y, int h, uint32_t c) { fill({x, y, 1, h}, c); }
    void line(int x0, int y0, int x1, int y1, uint32_t c);
    // Draws text with its top at y. Returns the advance in pixels. With
    // max_w > 0 the text is cut with an ellipsis to fit.
    int text(int x, int y, const char* s, uint32_t c, const Font& f = font_sans, int max_w = 0, int len = -1);
    void blit(int dx, int dy, const uint32_t* src, int sw, int sh, int sstride, Rect src_rect);
};

} // namespace gfx
