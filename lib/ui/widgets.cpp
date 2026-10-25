#include "fishos/ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ui {
using gfx::font_mono;
using gfx::font_sans;

void draw_button(gfx::Canvas& c, Rect r, const char* label, ButtonStyle style, bool pressed, bool disabled) {
    uint32_t bg = theme::field, fg = theme::text, edge = theme::border;
    if (style == Primary) { bg = theme::accent; fg = theme::accent_text; edge = theme::accent_dark; }
    if (style == Danger) { bg = theme::danger; fg = 0xFFFFFF; edge = 0x8E2C27; }
    if (style == Flat) { bg = theme::panel; edge = theme::panel; }
    if (pressed) bg = gfx::mix(bg, 0x000000, 40);
    if (disabled) { fg = gfx::mix(fg, bg, 150); }
    c.fill_rounded(r, 4, edge);
    c.fill_rounded(r.inset(1), 3, bg);
    int tw = gfx::text_width(font_sans, label);
    c.text(r.x + (r.w - tw) / 2, r.y + (r.h - font_sans.height) / 2, label, fg, font_sans, r.w - 8);
}

void draw_focus_ring(gfx::Canvas& c, Rect r) {
    c.frame(r, theme::coral);
    c.frame(r.inset(1), gfx::mix(theme::coral, 0xFFFFFF, 140));
}

void draw_icon(gfx::Canvas& c, Icon icon, int x, int y, int s) {
    int u = s / 16 > 0 ? s / 16 : 1; // pixels per design unit (16-unit grid)
    auto R = [&](int a, int b, int w, int h, uint32_t col) { c.fill({x + a * u, y + b * u, w * u, h * u}, col); };
    switch (icon) {
    case Icon::Folder: case Icon::NewFolder:
        R(1, 3, 6, 2, 0x3E8FA6);
        R(1, 4, 14, 10, 0x56A9C0);
        R(1, 6, 14, 8, 0x6DBDD2);
        if (icon == Icon::NewFolder) { R(10, 8, 4, 1, 0xFFFFFF); R(11, 7, 1, 3, 0xFFFFFF); R(12, 7, 0, 0, 0); }
        break;
    case Icon::File: case Icon::Text: case Icon::NewFile:
        R(3, 1, 10, 14, 0x8C9AA2);
        R(4, 2, 8, 12, 0xFFFFFF);
        R(9, 1, 4, 4, 0xD7DEE2);
        if (icon != Icon::File) for (int i = 0; i < 4; i++) R(5, 6 + i * 2, 6 - (i & 1) * 2, 1, 0x7D8D96);
        if (icon == Icon::NewFile) { R(9, 10, 5, 1, theme::coral); R(11, 8, 1, 5, theme::coral); }
        break;
    case Icon::Program:
        R(1, 2, 14, 12, 0x2E3F48);
        R(2, 4, 12, 9, 0x3B5260);
        R(3, 6, 2, 1, 0x9FE3C9); R(4, 7, 2, 1, 0x9FE3C9); R(3, 8, 2, 1, 0x9FE3C9);
        R(7, 9, 4, 1, 0x9FE3C9);
        break;
    case Icon::Files:
        R(1, 3, 6, 2, 0x2F7E95);
        R(1, 4, 14, 10, 0x3F96AE);
        R(1, 6, 14, 8, 0x5AB2C9);
        R(5, 9, 6, 1, 0xFFFFFF);
        break;
    case Icon::Editor:
        R(2, 1, 12, 14, 0xFFFFFF);
        R(2, 1, 12, 2, theme::coral);
        for (int i = 0; i < 5; i++) R(4, 5 + i * 2, 8 - (i % 2) * 3, 1, 0x6B7B84);
        c.frame({x + 2 * u, y + 1 * u, 12 * u, 14 * u}, 0x8C9AA2);
        break;
    case Icon::Terminal:
        R(1, 2, 14, 12, 0x1B262C);
        R(1, 2, 14, 2, 0x40525C);
        R(3, 6, 1, 1, 0x9FE3C9); R(4, 7, 1, 1, 0x9FE3C9); R(3, 8, 1, 1, 0x9FE3C9);
        R(6, 9, 5, 1, 0x9FE3C9);
        break;
    case Icon::Monitor:
        R(1, 2, 14, 10, 0x2B3A42);
        R(2, 3, 12, 8, 0x1C6A80);
        R(6, 12, 4, 2, 0x2B3A42);
        R(4, 14, 8, 1, 0x2B3A42);
        R(3, 8, 2, 2, 0xEE7B57); R(6, 6, 2, 4, 0x9FE3C9); R(9, 5, 2, 5, 0xFFFFFF); R(12, 7, 1, 3, 0x9FE3C9);
        break;
    case Icon::Up:
        for (int i = 0; i < 5; i++) R(8 - i, 3 + i, 1 + i * 2, 1, theme::text);
        R(7, 8, 3, 6, theme::text);
        break;
    case Icon::Back:
        for (int i = 0; i < 5; i++) R(3 + i, 8 - i, 1, 1 + i * 2, theme::text);
        R(8, 7, 6, 3, theme::text);
        break;
    }
}

// ---- LineEdit ----------------------------------------------------------------

void LineEdit::set(const char* s) {
    len = (int)strlcpy(text, s, sizeof(text));
    if (len > MAX) len = MAX;
    cursor = len;
    anchor = -1;
    scroll = 0;
}

void LineEdit::select_all() {
    anchor = 0;
    cursor = len;
}

void LineEdit::delete_selection() {
    if (!has_selection()) return;
    int a = anchor < cursor ? anchor : cursor, b = anchor < cursor ? cursor : anchor;
    memmove(text + a, text + b, (size_t)(len - b + 1));
    len -= b - a;
    cursor = a;
    anchor = -1;
}

bool LineEdit::key(const Event& e) {
    if (e.type != Event::Key) return false;
    bool sel = e.shift();
    auto move = [&](int to) {
        if (sel && anchor < 0) anchor = cursor;
        if (!sel) anchor = -1;
        cursor = to < 0 ? 0 : (to > len ? len : to);
    };
    if (e.ctrl()) {
        switch (e.ch) {
        case 'a': case 'A': select_all(); return true;
        case 'c': case 'C': case 'x': case 'X':
            if (has_selection()) {
                int a = anchor < cursor ? anchor : cursor, b = anchor < cursor ? cursor : anchor;
                clipboard_set(text + a, b - a);
                if ((e.ch | 32) == 'x') delete_selection();
            }
            return true;
        case 'v': case 'V': {
            int n;
            char* clip = clipboard_get(&n);
            if (clip) {
                delete_selection();
                for (int i = 0; i < n && len < MAX; i++) {
                    if (clip[i] < 32 || clip[i] > 126) continue; // single line: drop control chars
                    memmove(text + cursor + 1, text + cursor, (size_t)(len - cursor + 1));
                    text[cursor++] = clip[i];
                    len++;
                }
                free(clip);
            }
            return true;
        }
        }
    }
    switch (e.key) {
    case FIH_KEY_LEFT: move(has_selection() && !sel ? (anchor < cursor ? anchor : cursor) : cursor - 1); return true;
    case FIH_KEY_RIGHT: move(has_selection() && !sel ? (anchor > cursor ? anchor : cursor) : cursor + 1); return true;
    case FIH_KEY_HOME: move(0); return true;
    case FIH_KEY_END: move(len); return true;
    case FIH_KEY_BACKSPACE:
        if (has_selection()) delete_selection();
        else if (cursor > 0) {
            memmove(text + cursor - 1, text + cursor, (size_t)(len - cursor + 1));
            cursor--;
            len--;
        }
        return true;
    case FIH_KEY_DELETE:
        if (has_selection()) delete_selection();
        else if (cursor < len) {
            memmove(text + cursor, text + cursor + 1, (size_t)(len - cursor));
            len--;
        }
        return true;
    }
    if (e.ch >= 32 && e.ch < 127 && !e.ctrl() && !e.alt()) {
        delete_selection();
        if (len < MAX) {
            memmove(text + cursor + 1, text + cursor, (size_t)(len - cursor + 1));
            text[cursor++] = (char)e.ch;
            len++;
        }
        return true;
    }
    return false;
}

void LineEdit::click(const Event& e, Rect r) {
    int x = e.x - (r.x + 6) + scroll;
    int pos = x <= 0 ? 0 : (x + 4) / 8; // monospace: 8 px per character
    if (pos > len) pos = len;
    if (e.type == Event::MouseDown && e.clicks >= 2) { select_all(); return; }
    if (e.type == Event::MouseDown) {
        anchor = e.shift() ? (anchor < 0 ? cursor : anchor) : pos;
        cursor = pos;
    } else if (e.type == Event::MouseMove && (e.buttons & FIH_MOUSE_LEFT)) {
        if (anchor < 0) anchor = cursor;
        cursor = pos;
    }
}

void LineEdit::draw(gfx::Canvas& c, Rect r, bool focused) {
    c.fill(r, theme::field);
    c.frame(r, focused ? theme::accent : theme::border);
    if (focused) c.frame(r.inset(1), gfx::mix(theme::accent, 0xFFFFFF, 160));
    int inner = r.w - 12;
    int cx = cursor * 8;
    if (cx - scroll > inner) scroll = cx - inner;
    if (cx < scroll) scroll = cx;
    gfx::Rect old = c.clip;
    c.clip = r.inset(2).intersect(old);
    int ty = r.y + (r.h - font_mono.height) / 2;
    if (has_selection()) {
        int a = anchor < cursor ? anchor : cursor, b = anchor < cursor ? cursor : anchor;
        c.fill({r.x + 6 + a * 8 - scroll, ty, (b - a) * 8, font_mono.height}, focused ? theme::select_strong : theme::select);
        c.text(r.x + 6 - scroll, ty, text, theme::text, font_mono, 0, a);
        c.text(r.x + 6 + a * 8 - scroll, ty, text + a, focused ? 0xFFFFFF : theme::text, font_mono, 0, b - a);
        c.text(r.x + 6 + b * 8 - scroll, ty, text + b, theme::text, font_mono);
    } else {
        c.text(r.x + 6 - scroll, ty, text, theme::text, font_mono);
    }
    if (focused) c.fill({r.x + 6 + cx - scroll, ty, 2, font_mono.height}, theme::accent);
    c.clip = old;
}

// ---- Dialog --------------------------------------------------------------------

void Dialog::message_box(int t, const char* ttl, const char* msg) {
    kind = Message;
    tag = t;
    result = -1;
    pressed = -1;
    strlcpy(title, ttl, sizeof(title));
    strlcpy(message, msg, sizeof(message));
    buttons[0] = "OK";
    nbuttons = 1;
    cancel_index = 0;
}

void Dialog::confirm(int t, const char* ttl, const char* msg, const char* yes, const char* no, const char* third) {
    message_box(t, ttl, msg);
    kind = Confirm;
    buttons[0] = yes;
    buttons[1] = no;
    buttons[2] = third;
    nbuttons = third ? 3 : 2;
    cancel_index = nbuttons - 1;
}

void Dialog::prompt(int t, const char* ttl, const char* msg, const char* initial, const char* ok) {
    confirm(t, ttl, msg, ok, "Cancel");
    kind = Prompt;
    input.set(initial);
    // Preselect the base name so typing replaces it but keeps the extension.
    const char* dot = strrchr(input.text, '.');
    input.anchor = 0;
    input.cursor = dot && dot != input.text ? (int)(dot - input.text) : input.len;
}

static Rect button_rect(const Rect& box, int i, int n) {
    int bw = 96, gap = 8;
    int x = box.x + box.w - 16 - (n - i) * bw - (n - 1 - i) * gap;
    return {x, box.y + box.h - 16 - 30, bw, 30};
}

bool Dialog::handle(const Event& e) {
    if (!active()) return false;
    if (e.type == Event::Key) {
        if (e.key == FIH_KEY_ESC) { result = cancel_index; return true; }
        if (e.key == FIH_KEY_ENTER) {
            if (kind == Prompt && input.len == 0) return false;
            result = 0;
            return true;
        }
        if (kind == Prompt) input.key(e);
        return false;
    }
    if (e.type == Event::MouseDown || e.type == Event::MouseUp) {
        for (int i = 0; i < nbuttons; i++) {
            if (!button_rect(box, i, nbuttons).contains(e.x, e.y)) continue;
            if (e.type == Event::MouseDown) pressed = i;
            else if (pressed == i) {
                if (kind == Prompt && i == 0 && input.len == 0) return false;
                result = i;
                pressed = -1;
                return true;
            }
            return false;
        }
        pressed = -1;
        if (kind == Prompt && e.type == Event::MouseDown) {
            Rect field{box.x + 16, box.y + 76, box.w - 32, 30};
            if (field.contains(e.x, e.y)) input.click(e, field);
        }
    }
    if (e.type == Event::MouseMove && kind == Prompt) {
        Rect field{box.x + 16, box.y + 76, box.w - 32, 30};
        input.click(e, field);
    }
    return false;
}

void Dialog::draw(gfx::Canvas& c) {
    if (!active()) return;
    c.blend({0, 0, c.w, c.h}, 0x0B1E28, 90);
    int bw = c.w - 40 < 420 ? c.w - 40 : 420;
    int bh = kind == Prompt ? 170 : 140;
    box = {(c.w - bw) / 2, (c.h - bh) / 2, bw, bh};
    c.fill_rounded({box.x - 1, box.y - 1, box.w + 2, box.h + 2}, 7, 0x7F8C93);
    c.fill_rounded(box, 6, theme::bg);
    c.text(box.x + 16, box.y + 14, title, theme::text, font_sans, box.w - 32);
    c.hline(box.x + 16, box.y + 38, box.w - 32, theme::border);
    c.text(box.x + 16, box.y + 48, message, theme::text_dim, font_sans, box.w - 32);
    if (kind == Prompt) input.draw(c, {box.x + 16, box.y + 76, box.w - 32, 30}, true);
    for (int i = 0; i < nbuttons; i++)
        draw_button(c, button_rect(box, i, nbuttons), buttons[i], i == 0 ? Primary : Normal, pressed == i);
}

// ---- ListView ------------------------------------------------------------------

void ListView::clamp() {
    if (selected >= count) selected = count - 1;
    int max_top = count - visible();
    if (top > max_top) top = max_top;
    if (top < 0) top = 0;
}

void ListView::ensure_visible() {
    if (selected < 0) return clamp();
    if (selected < top) top = selected;
    if (selected >= top + visible()) top = selected - visible() + 1;
    clamp();
}

bool ListView::key(const Event& e) {
    if (e.type != Event::Key || count == 0) return false;
    int s = selected;
    switch (e.key) {
    case FIH_KEY_UP: s = s <= 0 ? 0 : s - 1; break;
    case FIH_KEY_DOWN: s = s < 0 ? 0 : s + 1; break;
    case FIH_KEY_PGUP: s -= visible(); break;
    case FIH_KEY_PGDN: s = (s < 0 ? 0 : s) + visible(); break;
    case FIH_KEY_HOME: s = 0; break;
    case FIH_KEY_END: s = count - 1; break;
    default: return false;
    }
    if (s < 0) s = 0;
    if (s >= count) s = count - 1;
    selected = s;
    ensure_visible();
    return true;
}

Rect ListView::row_rect(int i) const { return {area.x, area.y + (i - top) * row_h, area.w - 10, row_h}; }

int ListView::hit(int x, int y) const {
    if (!Rect{area.x, area.y, area.w - 10, area.h}.contains(x, y)) return -1;
    int i = top + (y - area.y) / row_h;
    return i < count ? i : -1;
}

static void thumb_geometry(const ListView& l, int* y, int* h) {
    int vis = l.visible();
    if (l.count <= vis) { *y = l.area.y; *h = l.area.h; return; }
    *h = l.area.h * vis / l.count;
    if (*h < 24) *h = 24;
    *y = l.area.y + (l.area.h - *h) * l.top / (l.count - vis);
}

bool ListView::mouse(const Event& e) {
    if (e.type == Event::Wheel && area.contains(e.x, e.y)) {
        top += e.wheel * 3;
        clamp();
        return true;
    }
    Rect track{area.x + area.w - 10, area.y, 10, area.h};
    int ty, th;
    thumb_geometry(*this, &ty, &th);
    if (e.type == Event::MouseDown && track.contains(e.x, e.y) && count > visible()) {
        if (e.y >= ty && e.y < ty + th) {
            dragging_thumb = true;
            drag_offset = e.y - ty;
        } else {
            top += e.y < ty ? -visible() : visible();
            clamp();
        }
        return true;
    }
    if (e.type == Event::MouseMove && dragging_thumb) {
        int range = area.h - th;
        if (range > 0) top = (e.y - drag_offset - area.y) * (count - visible()) / range;
        clamp();
        return true;
    }
    if (e.type == Event::MouseUp && dragging_thumb) {
        dragging_thumb = false;
        return true;
    }
    return false;
}

void ListView::draw_scrollbar(gfx::Canvas& c) const {
    Rect track{area.x + area.w - 10, area.y, 10, area.h};
    c.fill(track, theme::bg);
    if (count <= visible()) return;
    int ty, th;
    thumb_geometry(*this, &ty, &th);
    c.fill_rounded({track.x + 2, ty + 2, 6, th - 4}, 3, dragging_thumb ? 0x7D8D96 : 0xAAB6BC);
}

void format_size(char* out, int n, uint32_t b) {
    if (b < 1024) snprintf(out, (size_t)n, "%u B", b);
    else if (b < 1024 * 1024) snprintf(out, (size_t)n, "%u.%u KB", b / 1024, (b % 1024) * 10 / 1024);
    else snprintf(out, (size_t)n, "%u.%u MB", b >> 20, ((b >> 10) % 1024) * 10 / 1024);
}

void format_time(char* out, int n, uint32_t t) {
    uint32_t sec = t % 60, min = (t / 60) % 60, hour = (t / 3600) % 24, days = t / 86400;
    uint32_t year = 2000;
    for (;;) {
        uint32_t ydays = (year % 4 == 0) ? 366 : 365;
        if (days < ydays) break;
        days -= ydays;
        year++;
    }
    static const uint8_t md[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint32_t month = 0;
    for (; month < 12; month++) {
        uint32_t d = md[month] + (month == 1 && year % 4 == 0 ? 1 : 0);
        if (days < d) break;
        days -= d;
    }
    (void)sec;
    snprintf(out, (size_t)n, "%04u-%02u-%02u %02u:%02u", year, month + 1, days + 1, hour, min);
}

} // namespace ui
