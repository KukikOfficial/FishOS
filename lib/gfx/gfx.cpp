#include "fishos/gfx.h"
#include <string.h>

namespace gfx {

Rect Rect::intersect(const Rect& o) const {
    int x0 = x > o.x ? x : o.x, y0 = y > o.y ? y : o.y;
    int x1 = x + w < o.x + o.w ? x + w : o.x + o.w;
    int y1 = y + h < o.y + o.h ? y + h : o.y + o.h;
    if (x1 <= x0 || y1 <= y0) return {};
    return {x0, y0, x1 - x0, y1 - y0};
}

Rect Rect::unite(const Rect& o) const {
    if (empty()) return o;
    if (o.empty()) return *this;
    int x0 = x < o.x ? x : o.x, y0 = y < o.y ? y : o.y;
    int x1 = x + w > o.x + o.w ? x + w : o.x + o.w;
    int y1 = y + h > o.y + o.h ? y + h : o.y + o.h;
    return {x0, y0, x1 - x0, y1 - y0};
}

uint32_t mix(uint32_t a, uint32_t b, uint32_t t) {
    uint32_t it = 255 - t;
    uint32_t rb = ((a & 0xFF00FF) * it + (b & 0xFF00FF) * t) >> 8;
    uint32_t g = ((a & 0x00FF00) * it + (b & 0x00FF00) * t) >> 8;
    return (rb & 0xFF00FF) | (g & 0x00FF00);
}

static const Glyph& glyph(const Font& f, char c) {
    unsigned char u = (unsigned char)c;
    return f.glyphs[(u >= 32 && u < 127) ? u - 32 : '?' - 32];
}

int text_width(const Font& f, const char* s, int len) {
    int w = 0;
    for (int i = 0; s[i] && (len < 0 || i < len); i++) w += glyph(f, s[i]).advance;
    return w;
}

int text_fit(const Font& f, const char* s, int max_w) {
    int w = 0, i = 0;
    for (; s[i]; i++) {
        w += glyph(f, s[i]).advance;
        if (w > max_w) break;
    }
    return i;
}

void Canvas::reset(uint32_t* p, int width, int height, int stride_px) {
    px = p;
    w = width;
    h = height;
    stride = stride_px;
    clip = {0, 0, width, height};
}

void Canvas::fill(Rect r, uint32_t c) {
    r = r.intersect(clip);
    for (int y = r.y; y < r.y + r.h; y++) {
        uint32_t* row = px + y * stride + r.x;
        for (int x = 0; x < r.w; x++) row[x] = c;
    }
}

void Canvas::blend(Rect r, uint32_t c, uint32_t alpha) {
    r = r.intersect(clip);
    for (int y = r.y; y < r.y + r.h; y++) {
        uint32_t* row = px + y * stride + r.x;
        for (int x = 0; x < r.w; x++) row[x] = mix(row[x], c, alpha);
    }
}

static int isqrt(int v) {
    int r = 0;
    while ((r + 1) * (r + 1) <= v) r++;
    return r;
}

void Canvas::fill_rounded(Rect r, int rad, uint32_t c) {
    if (rad * 2 > r.h) rad = r.h / 2;
    if (rad * 2 > r.w) rad = r.w / 2;
    for (int i = 0; i < r.h; i++) {
        int d = i < rad ? rad - 1 - i : (i >= r.h - rad ? i - (r.h - rad) : -1);
        int inset = d < 0 ? 0 : rad - isqrt(rad * rad * 4 - (2 * d + 1) * (2 * d + 1)) / 2;
        fill({r.x + inset, r.y + i, r.w - 2 * inset, 1}, c);
        if (inset > 0) { // soften the curve with one blended pixel per side
            blend({r.x + inset - 1, r.y + i, 1, 1}, c, 96);
            blend({r.x + r.w - inset, r.y + i, 1, 1}, c, 96);
        }
    }
}

void Canvas::frame(Rect r, uint32_t c) {
    hline(r.x, r.y, r.w, c);
    hline(r.x, r.y + r.h - 1, r.w, c);
    vline(r.x, r.y, r.h, c);
    vline(r.x + r.w - 1, r.y, r.h, c);
}

void Canvas::line(int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (clip.contains(x0, y0)) px[y0 * stride + x0] = c;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

int Canvas::text(int x, int y, const char* s, uint32_t c, const Font& f, int max_w, int len) {
    if (len < 0) len = (int)strlen(s);
    int ellipsis = 0;
    if (max_w > 0 && text_width(f, s, len) > max_w) {
        int dots = text_width(f, "...");
        len = text_fit(f, s, max_w - dots > 0 ? max_w - dots : 0);
        ellipsis = 3;
    }
    int pen = x, base = y + f.baseline;
    for (int i = 0; i < len + ellipsis; i++) {
        const Glyph& g = glyph(f, i < len ? s[i] : '.');
        const uint8_t* a = f.alpha + g.offset;
        int gx = pen + g.xoff, gy = base + g.yoff;
        Rect gr = Rect{gx, gy, g.w, g.h}.intersect(clip);
        for (int yy = gr.y; yy < gr.y + gr.h; yy++) {
            uint32_t* row = px + yy * stride;
            const uint8_t* arow = a + (yy - gy) * g.w;
            for (int xx = gr.x; xx < gr.x + gr.w; xx++) {
                uint32_t al = arow[xx - gx];
                if (al == 255) row[xx] = c;
                else if (al) row[xx] = mix(row[xx], c, al);
            }
        }
        pen += g.advance;
    }
    return pen - x;
}

void Canvas::blit(int dx, int dy, const uint32_t* src, int sw, int sh, int sstride, Rect sr) {
    sr = sr.intersect({0, 0, sw, sh});
    Rect dst = Rect{dx, dy, sr.w, sr.h}.intersect(clip);
    if (dst.empty()) return;
    int ox = sr.x + (dst.x - dx), oy = sr.y + (dst.y - dy);
    for (int y = 0; y < dst.h; y++)
        memcpy(px + (dst.y + y) * stride + dst.x, src + (oy + y) * sstride + ox, (size_t)dst.w * 4);
}

} // namespace gfx
