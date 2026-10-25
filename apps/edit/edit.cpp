// FishOS Text Editor.
#include <fihcore/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fishos/fs.h"
#include "fishos/ui.h"

using gfx::Rect;
using gfx::font_mono;
using gfx::font_sans;
using ui::Event;
namespace theme = ui::theme;

namespace {
constexpr int LINE_H = 18;
constexpr int CHAR_W = 8;

struct Line {
    char* s;
    int len, cap;
};

struct Pos {
    int row, col;
    bool operator==(const Pos& o) const { return row == o.row && col == o.col; }
    bool operator<(const Pos& o) const { return row < o.row || (row == o.row && col < o.col); }
};

ui::Window win;
ui::Dialog dialog;
Line* lines;
int nlines, lines_cap;
Pos cur, anchor{-1, -1};
int top, left;
int want_col = -1;    // column kept while moving vertically
bool modified;
char path[FIH_PATH_MAX];
char status[128];
int pressed_button = -1;
bool selecting, dragging_thumb;
int thumb_grab;

enum { DLG_ERROR = 1, DLG_OPEN, DLG_SAVE_AS, DLG_UNSAVED };
enum PendingAction { ACT_NONE, ACT_NEW, ACT_OPEN, ACT_QUIT };
PendingAction pending;

// ---- buffer ----------------------------------------------------------------
bool reserve(Line& l, int need) {
    if (need + 1 <= l.cap) return true;
    int cap = l.cap ? l.cap : 16;
    while (cap < need + 1) cap *= 2;
    char* s = static_cast<char*>(realloc(l.s, (size_t)cap));
    if (!s) return false;
    l.s = s;
    l.cap = cap;
    return true;
}

bool insert_lines(int at, int count) {
    if (nlines + count > lines_cap) {
        int cap = lines_cap ? lines_cap : 64;
        while (cap < nlines + count) cap *= 2;
        Line* nl = static_cast<Line*>(realloc(lines, sizeof(Line) * (size_t)cap));
        if (!nl) return false;
        lines = nl;
        lines_cap = cap;
    }
    memmove(lines + at + count, lines + at, sizeof(Line) * (size_t)(nlines - at));
    for (int i = 0; i < count; i++) lines[at + i] = Line{nullptr, 0, 0};
    nlines += count;
    for (int i = 0; i < count; i++) reserve(lines[at + i], 0), lines[at + i].s[0] = 0;
    return true;
}

void remove_lines(int at, int count) {
    for (int i = 0; i < count; i++) free(lines[at + i].s);
    memmove(lines + at, lines + at + count, sizeof(Line) * (size_t)(nlines - at - count));
    nlines -= count;
}

void clear_doc() {
    for (int i = 0; i < nlines; i++) free(lines[i].s);
    nlines = 0;
    insert_lines(0, 1);
    cur = {0, 0};
    anchor = {-1, -1};
    top = left = 0;
}

bool has_sel() { return anchor.row >= 0 && !(anchor == cur); }
void sel_range(Pos* a, Pos* b) {
    if (anchor < cur) { *a = anchor; *b = cur; }
    else { *a = cur; *b = anchor; }
}

// Inserts text (may contain newlines) at the cursor.
bool insert_text(const char* t, int n) {
    for (int i = 0; i < n; i++) {
        char c = t[i];
        if (c == '\r') continue;
        Line& l = lines[cur.row];
        if (c == '\n') {
            if (!insert_lines(cur.row + 1, 1)) return false;
            Line& a = lines[cur.row];
            Line& b = lines[cur.row + 1];
            int tail = a.len - cur.col;
            if (!reserve(b, tail)) return false;
            memcpy(b.s, a.s + cur.col, (size_t)tail);
            b.len = tail;
            b.s[tail] = 0;
            a.len = cur.col;
            a.s[a.len] = 0;
            cur = {cur.row + 1, 0};
            continue;
        }
        if (c == '\t') {
            insert_text("    ", 4 - (cur.col % 4));
            continue;
        }
        if ((unsigned char)c < 32) c = '?';
        if (!reserve(l, l.len + 1)) return false;
        memmove(l.s + cur.col + 1, l.s + cur.col, (size_t)(l.len - cur.col + 1));
        l.s[cur.col++] = c;
        l.len++;
    }
    modified = true;
    return true;
}

void delete_range(Pos a, Pos b) {
    if (a.row == b.row) {
        Line& l = lines[a.row];
        memmove(l.s + a.col, l.s + b.col, (size_t)(l.len - b.col + 1));
        l.len -= b.col - a.col;
    } else {
        Line& first = lines[a.row];
        Line& last = lines[b.row];
        int tail = last.len - b.col;
        reserve(first, a.col + tail);
        memcpy(first.s + a.col, last.s + b.col, (size_t)tail + 1);
        first.len = a.col + tail;
        remove_lines(a.row + 1, b.row - a.row);
    }
    cur = a;
    anchor = {-1, -1};
    modified = true;
}

bool delete_selection() {
    if (!has_sel()) return false;
    Pos a, b;
    sel_range(&a, &b);
    delete_range(a, b);
    return true;
}

char* range_text(Pos a, Pos b, int* out_len) {
    int n = 0;
    for (int r = a.row; r <= b.row; r++) {
        int s = r == a.row ? a.col : 0, e = r == b.row ? b.col : lines[r].len;
        n += e - s + (r < b.row ? 1 : 0);
    }
    char* t = static_cast<char*>(malloc((size_t)n + 1));
    if (!t) return nullptr;
    int o = 0;
    for (int r = a.row; r <= b.row; r++) {
        int s = r == a.row ? a.col : 0, e = r == b.row ? b.col : lines[r].len;
        memcpy(t + o, lines[r].s + s, (size_t)(e - s));
        o += e - s;
        if (r < b.row) t[o++] = '\n';
    }
    t[o] = 0;
    *out_len = o;
    return t;
}

// ---- files -----------------------------------------------------------------
void update_title() {
    char t[96];
    snprintf(t, sizeof t, "%s%s - Text Editor", modified ? "* " : "", path[0] ? fs::basename(path) : "Untitled");
    win.set_title(t);
}

int load(const char* p) {
    char* data;
    uint32_t len;
    int e = fs::read_all(p, &data, &len);
    if (e) return e;
    clear_doc();
    insert_text(data, (int)len);
    free(data);
    cur = {0, 0};
    modified = false;
    strlcpy(path, p, sizeof path);
    return 0;
}

int save(const char* p) {
    int total = 0;
    for (int i = 0; i < nlines; i++) total += lines[i].len + 1;
    char* data = static_cast<char*>(malloc((size_t)total + 1));
    if (!data) return -FIH_ENOMEM;
    int o = 0;
    for (int i = 0; i < nlines; i++) {
        memcpy(data + o, lines[i].s, (size_t)lines[i].len);
        o += lines[i].len;
        if (i < nlines - 1) data[o++] = '\n';
    }
    int e = fs::write_atomic(p, data, (uint32_t)o);
    free(data);
    if (e) return e;
    strlcpy(path, p, sizeof path);
    modified = false;
    snprintf(status, sizeof status, "Saved %d bytes", o);
    return 0;
}

void show_error(const char* what, int e) {
    char msg[200];
    snprintf(msg, sizeof msg, "%s: %s.", what, ui::error_string(e));
    dialog.message_box(DLG_ERROR, "Text Editor", msg);
}

void ask_path(int tag, const char* title, const char* ok) {
    char initial[FIH_PATH_MAX];
    if (path[0]) strlcpy(initial, path, sizeof initial);
    else fs::join(fs::is_dir("/home/Documents") ? "/home/Documents" : "/home", "untitled.txt", initial, sizeof initial);
    dialog.prompt(tag, title, "File path:", initial, ok);
}

void do_save_as() { ask_path(DLG_SAVE_AS, "Save As", "Save"); }

// Returns true if saved immediately; otherwise a dialog was opened.
bool do_save() {
    if (!path[0]) {
        do_save_as();
        return false;
    }
    int e = save(path);
    if (e) show_error("Could not save the file", e);
    update_title();
    return e == 0;
}

void run_pending() {
    PendingAction a = pending;
    pending = ACT_NONE;
    if (a == ACT_NEW) {
        clear_doc();
        path[0] = 0;
        modified = false;
        update_title();
    } else if (a == ACT_OPEN) {
        ask_path(DLG_OPEN, "Open", "Open");
    } else if (a == ACT_QUIT) {
        win.close();
        fih_exit(0);
    }
}

void guard(PendingAction a) {
    pending = a;
    if (!modified) return run_pending();
    dialog.confirm(DLG_UNSAVED, "Unsaved changes",
                   path[0] ? "Save changes to this document before continuing?" : "Save this new document first?",
                   "Save", "Don't Save", "Cancel");
}

// ---- layout & drawing ---------------------------------------------------------
Rect toolbar() { return {0, 0, win.w, theme::toolbar_h}; }
Rect text_area() { return {0, theme::toolbar_h, win.w, win.h - theme::toolbar_h - theme::status_h}; }
int gutter_w() { return 8 + CHAR_W * (nlines >= 10000 ? 5 : 4) + 8; }
int rows_visible() { return text_area().h / LINE_H; }
int cols_visible() { return (text_area().w - gutter_w() - 14) / CHAR_W; }
const char* const BUTTONS[4] = {"New", "Open", "Save", "Save As"};
Rect button(int i) { return {8 + i * 86, 6, 80, 28}; }

void scroll_to_cursor() {
    int rows = rows_visible();
    if (cur.row < top) top = cur.row;
    if (cur.row >= top + rows) top = cur.row - rows + 1;
    int cols = cols_visible();
    if (cur.col < left) left = cur.col;
    if (cur.col >= left + cols) left = cur.col - cols + 1;
    if (top < 0) top = 0;
    if (left < 0) left = 0;
}

void draw() {
    gfx::Canvas& c = win.canvas;
    c.clip = {0, 0, win.w, win.h};
    c.fill(toolbar(), theme::panel);
    c.hline(0, theme::toolbar_h - 1, win.w, theme::border);
    for (int i = 0; i < 4; i++) ui::draw_button(c, button(i), BUTTONS[i], i == 2 ? ui::Primary : ui::Normal, pressed_button == i);

    Rect ta = text_area();
    c.fill(ta, theme::field);
    int gw = gutter_w();
    c.fill({0, ta.y, gw - 4, ta.h}, 0xF0F3F4);
    Pos sa{0, 0}, sb{0, 0};
    bool sel = has_sel();
    if (sel) sel_range(&sa, &sb);
    c.clip = ta;
    for (int i = 0; i <= rows_visible() && top + i < nlines; i++) {
        int row = top + i, y = ta.y + 2 + i * LINE_H;
        char num[12];
        snprintf(num, sizeof num, "%d", row + 1);
        c.text(gw - 8 - (int)strlen(num) * CHAR_W, y, num, row == cur.row ? theme::text : 0xA0ACB2, font_mono);
        Line& l = lines[row];
        int x0 = gw + 4;
        c.clip = Rect{gw, ta.y, ta.w - gw - 12, ta.h}.intersect(ta);
        if (sel && row >= sa.row && row <= sb.row) {
            int s = row == sa.row ? sa.col : 0, e = row == sb.row ? sb.col : l.len + 1;
            c.fill({x0 + (s - left) * CHAR_W, y - 1, (e - s) * CHAR_W, LINE_H}, win.focused ? 0xBFDDE7 : theme::select);
        }
        if (l.len > left) c.text(x0, y, l.s + left, theme::text, font_mono, 0, l.len - left);
        if (row == cur.row && win.focused && !dialog.active())
            c.fill({x0 + (cur.col - left) * CHAR_W, y - 1, 2, LINE_H}, theme::accent);
        c.clip = ta;
    }
    c.clip = {0, 0, win.w, win.h};
    // Vertical scrollbar.
    Rect track{ta.x + ta.w - 10, ta.y, 10, ta.h};
    c.fill(track, theme::bg);
    if (nlines > rows_visible()) {
        int th = ta.h * rows_visible() / nlines;
        if (th < 24) th = 24;
        int ty = ta.y + (ta.h - th) * top / (nlines - rows_visible() > 0 ? nlines - rows_visible() : 1);
        c.fill_rounded({track.x + 2, ty + 2, 6, th - 4}, 3, dragging_thumb ? 0x7D8D96 : 0xAAB6BC);
    }
    // Status bar.
    Rect sbar{0, win.h - theme::status_h, win.w, theme::status_h};
    c.fill(sbar, theme::panel);
    c.hline(0, sbar.y, win.w, theme::border);
    char pos[48];
    snprintf(pos, sizeof pos, "Ln %d, Col %d   %d lines", cur.row + 1, cur.col + 1, nlines);
    int pw = gfx::text_width(font_sans, pos);
    c.text(sbar.w - pw - 12, sbar.y + 4, pos, theme::text_dim, font_sans);
    char left_text[FIH_PATH_MAX + 64];
    snprintf(left_text, sizeof left_text, "%s%s%s%s", modified ? "Modified  " : "", path[0] ? path : "Untitled",
             status[0] ? "  -  " : "", status);
    c.text(10, sbar.y + 4, left_text, modified ? theme::coral : theme::text_dim, font_sans, sbar.w - pw - 40);
    dialog.draw(c);
    win.present();
}

// ---- input -------------------------------------------------------------------
void move_to(Pos p, bool extend) {
    if (extend && anchor.row < 0) anchor = cur;
    if (!extend) anchor = {-1, -1};
    if (p.row < 0) p.row = 0;
    if (p.row >= nlines) p.row = nlines - 1;
    if (p.col < 0) p.col = 0;
    if (p.col > lines[p.row].len) p.col = lines[p.row].len;
    cur = p;
}

bool is_word(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

Pos word_left(Pos p) {
    if (p.col == 0) return p.row > 0 ? Pos{p.row - 1, lines[p.row - 1].len} : p;
    const char* s = lines[p.row].s;
    while (p.col > 0 && !is_word(s[p.col - 1])) p.col--;
    while (p.col > 0 && is_word(s[p.col - 1])) p.col--;
    return p;
}

Pos word_right(Pos p) {
    Line& l = lines[p.row];
    if (p.col == l.len) return p.row < nlines - 1 ? Pos{p.row + 1, 0} : p;
    while (p.col < l.len && !is_word(l.s[p.col])) p.col++;
    while (p.col < l.len && is_word(l.s[p.col])) p.col++;
    return p;
}

void copy_selection(bool cut) {
    if (!has_sel()) return;
    Pos a, b;
    sel_range(&a, &b);
    int n;
    char* t = range_text(a, b, &n);
    if (!t) return;
    if (!ui::clipboard_set(t, n)) snprintf(status, sizeof status, "Clipboard unavailable");
    free(t);
    if (cut) delete_selection();
}

void paste() {
    int n;
    char* t = ui::clipboard_get(&n);
    if (!t) return;
    delete_selection();
    if (!insert_text(t, n)) snprintf(status, sizeof status, "Out of memory");
    free(t);
}

void on_key(const Event& e) {
    bool sh = e.shift();
    int vcol = want_col >= 0 ? want_col : cur.col;
    bool vertical = false;
    status[0] = 0;
    if (e.ctrl()) {
        switch (e.ch | 32) {
        case 's': if (sh) do_save_as(); else do_save(); return;
        case 'o': guard(ACT_OPEN); return;
        case 'n': guard(ACT_NEW); return;
        case 'q': guard(ACT_QUIT); return;
        case 'a': anchor = {0, 0}; cur = {nlines - 1, lines[nlines - 1].len}; return;
        case 'c': copy_selection(false); return;
        case 'x': copy_selection(true); scroll_to_cursor(); return;
        case 'v': paste(); scroll_to_cursor(); return;
        }
    }
    switch (e.key) {
    case FIH_KEY_LEFT:
        if (has_sel() && !sh) { Pos a, b; sel_range(&a, &b); move_to(a, false); break; }
        if (e.ctrl()) move_to(word_left(cur), sh);
        else if (cur.col > 0) move_to({cur.row, cur.col - 1}, sh);
        else if (cur.row > 0) move_to({cur.row - 1, lines[cur.row - 1].len}, sh);
        break;
    case FIH_KEY_RIGHT:
        if (has_sel() && !sh) { Pos a, b; sel_range(&a, &b); move_to(b, false); break; }
        if (e.ctrl()) move_to(word_right(cur), sh);
        else if (cur.col < lines[cur.row].len) move_to({cur.row, cur.col + 1}, sh);
        else if (cur.row < nlines - 1) move_to({cur.row + 1, 0}, sh);
        break;
    case FIH_KEY_UP: move_to({cur.row - 1, vcol}, sh); vertical = true; break;
    case FIH_KEY_DOWN: move_to({cur.row + 1, vcol}, sh); vertical = true; break;
    case FIH_KEY_PGUP: move_to({cur.row - rows_visible(), vcol}, sh); top -= rows_visible(); vertical = true; break;
    case FIH_KEY_PGDN: move_to({cur.row + rows_visible(), vcol}, sh); top += rows_visible(); vertical = true; break;
    case FIH_KEY_HOME: move_to(e.ctrl() ? Pos{0, 0} : Pos{cur.row, 0}, sh); break;
    case FIH_KEY_END: move_to(e.ctrl() ? Pos{nlines - 1, lines[nlines - 1].len} : Pos{cur.row, lines[cur.row].len}, sh); break;
    case FIH_KEY_BACKSPACE:
        if (delete_selection()) break;
        if (cur.col > 0) delete_range({cur.row, cur.col - 1}, cur);
        else if (cur.row > 0) delete_range({cur.row - 1, lines[cur.row - 1].len}, cur);
        break;
    case FIH_KEY_DELETE:
        if (delete_selection()) break;
        if (cur.col < lines[cur.row].len) delete_range(cur, {cur.row, cur.col + 1});
        else if (cur.row < nlines - 1) delete_range(cur, {cur.row + 1, 0});
        break;
    case FIH_KEY_ENTER: {
        delete_selection();
        // Keep the indentation of the current line.
        int indent = 0;
        while (indent < lines[cur.row].len && indent < cur.col && lines[cur.row].s[indent] == ' ') indent++;
        insert_text("\n", 1);
        for (int i = 0; i < indent; i++) insert_text(" ", 1);
        break;
    }
    case FIH_KEY_TAB:
        delete_selection();
        insert_text("\t", 1);
        break;
    default:
        if (e.ch >= 32 && e.ch < 127 && !e.ctrl() && !e.alt()) {
            delete_selection();
            char ch = (char)e.ch;
            insert_text(&ch, 1);
        } else {
            return;
        }
    }
    want_col = vertical ? vcol : -1;
    if (top > nlines - 1) top = nlines - 1;
    if (top < 0) top = 0;
    scroll_to_cursor();
}

Pos pos_at(int x, int y) {
    Rect ta = text_area();
    int row = top + (y - ta.y - 2) / LINE_H;
    if (y < ta.y) row = top - 1;
    int col = left + (x - gutter_w() - 4 + CHAR_W / 2) / CHAR_W;
    if (row >= nlines) row = nlines - 1;
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    if (col > lines[row].len) col = lines[row].len;
    return {row, col};
}

void scroll_by(int rows) {
    top += rows;
    if (top > nlines - rows_visible()) top = nlines - rows_visible();
    if (top < 0) top = 0;
}

void on_mouse(const Event& e) {
    Rect ta = text_area();
    Rect track{ta.x + ta.w - 10, ta.y, 10, ta.h};
    if (e.type == Event::Wheel) { scroll_by(e.wheel * 3); return; }
    if (e.type == Event::MouseDown) {
        for (int i = 0; i < 4; i++) if (button(i).contains(e.x, e.y)) { pressed_button = i; return; }
        if (track.contains(e.x, e.y) && nlines > rows_visible()) {
            int th = ta.h * rows_visible() / nlines;
            if (th < 24) th = 24;
            int ty = ta.y + (ta.h - th) * top / (nlines - rows_visible());
            if (e.y >= ty && e.y < ty + th) { dragging_thumb = true; thumb_grab = e.y - ty; }
            else scroll_by(e.y < ty ? -rows_visible() : rows_visible());
            return;
        }
        if (ta.contains(e.x, e.y)) {
            Pos p = pos_at(e.x, e.y);
            if (e.clicks == 2) { // select the word under the pointer
                Line& l = lines[p.row];
                int a = p.col, b = p.col;
                while (a > 0 && is_word(l.s[a - 1])) a--;
                while (b < l.len && is_word(l.s[b])) b++;
                anchor = {p.row, a};
                cur = {p.row, b};
            } else if (e.clicks >= 3) {
                anchor = {p.row, 0};
                cur = {p.row, lines[p.row].len};
            } else {
                move_to(p, e.shift());
                if (!e.shift()) anchor = cur;
                selecting = true;
            }
            want_col = -1;
        }
        return;
    }
    if (e.type == Event::MouseMove) {
        if (dragging_thumb) {
            int th = ta.h * rows_visible() / nlines;
            if (th < 24) th = 24;
            int range = ta.h - th;
            if (range > 0) top = (e.y - thumb_grab - ta.y) * (nlines - rows_visible()) / range;
            scroll_by(0);
        } else if (selecting) {
            cur = pos_at(e.x, e.y);
            if (e.y < ta.y) scroll_by(-1);
            if (e.y >= ta.y + ta.h) scroll_by(1);
        }
        return;
    }
    if (e.type == Event::MouseUp) {
        if (pressed_button >= 0 && button(pressed_button).contains(e.x, e.y)) {
            int b = pressed_button;
            pressed_button = -1;
            if (b == 0) guard(ACT_NEW);
            else if (b == 1) guard(ACT_OPEN);
            else if (b == 2) do_save();
            else do_save_as();
        }
        pressed_button = -1;
        dragging_thumb = false;
        selecting = false;
        if (anchor == cur) anchor = {-1, -1};
    }
}

void dialog_done() {
    int tag = dialog.tag, r = dialog.result;
    char text[FIH_PATH_MAX];
    strlcpy(text, dialog.input.text, sizeof text);
    dialog.close();
    if (tag == DLG_UNSAVED) {
        if (r == 2) { pending = ACT_NONE; return; }   // cancel
        if (r == 1) { modified = false; run_pending(); return; } // discard
        if (do_save()) run_pending();
        // otherwise Save As is open and pending runs after it
        return;
    }
    if ((tag == DLG_OPEN || tag == DLG_SAVE_AS) && r == 0) {
        char abs[FIH_PATH_MAX];
        int e = fs::absolute(text, abs, sizeof abs);
        if (!e && tag == DLG_OPEN) {
            e = load(abs);
            if (e) show_error("Could not open the file", e);
        } else if (!e) {
            e = save(abs);
            if (e) show_error("Could not save the file", e);
            else if (pending != ACT_NONE) { update_title(); run_pending(); }
        } else {
            show_error("Invalid path", e);
        }
        update_title();
        return;
    }
    if (tag != DLG_ERROR) pending = ACT_NONE;
}
}

int main(int argc, char** argv) {
    clear_doc();
    if (!win.open("Text Editor", 720, 500)) return 1;
    if (argc > 1) {
        char abs[FIH_PATH_MAX];
        if (fs::absolute(argv[1], abs, sizeof abs) == 0) {
            int e = load(abs);
            if (e == -FIH_ENOENT) { // new document that will be created on save
                strlcpy(path, abs, sizeof path);
                snprintf(status, sizeof status, "New file");
            } else if (e) {
                show_error("Could not open the file", e);
            }
        }
    }
    update_title();
    draw();
    for (;;) {
        ui::Window* w;
        Event e;
        if (!ui::next_event(&w, &e, -1)) continue;
        bool was_modified = modified;
        if (dialog.active()) {
            if (e.type == Event::Close) { dialog.close(); guard(ACT_QUIT); }
            else if (dialog.handle(e)) dialog_done();
        } else {
            switch (e.type) {
            case Event::Key: on_key(e); break;
            case Event::MouseDown: case Event::MouseUp: case Event::MouseMove: case Event::Wheel: on_mouse(e); break;
            case Event::Close: guard(ACT_QUIT); break;
            case Event::Resize: scroll_to_cursor(); break;
            default: break;
            }
        }
        if (was_modified != modified) update_title();
        draw();
    }
}
