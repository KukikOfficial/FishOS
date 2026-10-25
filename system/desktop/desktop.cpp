// FishOS desktop: display server, compositor, window manager, taskbar and
// application launcher. Owns the framebuffer and all input; every application
// is a separate process that draws into a shared buffer (see wsproto.h).
#include <fihcore/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fishos/gfx.h"
#include "fishos/ui.h"
#include "fishos/wsproto.h"

using gfx::Rect;
using gfx::font_sans;
namespace theme = ui::theme;

namespace {
constexpr int MAX_WIN = 32;
constexpr int TITLE_H = 30;
constexpr int TASKBAR_H = 40;
constexpr int SHADOW = 3;
constexpr int MIN_W = 220, MIN_H = 120;
constexpr uint32_t DESK_TOP = 0x14394A, DESK_BOTTOM = 0x0D2633;
constexpr uint32_t BAR_BG = 0x0C1F28, BAR_TEXT = 0xD8E6EC, BAR_ACTIVE = 0x24586A;

struct Win {
    bool used;
    int id;
    uint32_t owner;
    int shm;
    uint32_t* buf;
    int bw, bh;           // client buffer size
    int x, y;             // frame position (content is at x+1, y+TITLE_H)
    int cw, ch;           // content size the frame is laid out for
    char title[64];
    uint32_t flags;
    bool minimized, maximized;
    Rect restore;         // frame geometry before maximize
};

fih_display_t disp;
gfx::Canvas screen, back;
int SW, SH;
Win wins[MAX_WIN];
int zorder[MAX_WIN], nz; // indices into wins, bottom to top
int focus = -1;
int next_id = 1;
uint32_t desk_row[2048];

// Damage: a short list of rectangles, merged when it overflows.
constexpr int MAX_DIRTY = 8;
Rect dirty[MAX_DIRTY];
int ndirty;

void damage(Rect r) {
    r = r.intersect({0, 0, SW, SH});
    if (r.empty()) return;
    for (int i = 0; i < ndirty; i++) {
        if (!dirty[i].intersect(r).empty() || dirty[i].unite(r).w * dirty[i].unite(r).h <
                                                  dirty[i].w * dirty[i].h + r.w * r.h + 4096) {
            dirty[i] = dirty[i].unite(r);
            return;
        }
    }
    if (ndirty < MAX_DIRTY) { dirty[ndirty++] = r; return; }
    for (int i = 1; i < ndirty; i++) dirty[0] = dirty[0].unite(dirty[i]);
    dirty[0] = dirty[0].unite(r);
    ndirty = 1;
}

// ---- input state ---------------------------------------------------------
int mx, my;
uint32_t buttons;
int capture = -1;              // window receiving mouse events while a button is held
enum Drag { None, Move, Resize, TitleButton, TaskButton, Launcher } drag = None;
int drag_win = -1, drag_dx, drag_dy, title_button;
Rect resize_outline;
uint32_t last_click_time;
int last_click_x, last_click_y, click_count;

// ---- launcher ------------------------------------------------------------
struct AppEntry {
    char name[40];
    char exec[96];
    ui::Icon icon;
};
constexpr int MAX_APPS = 16;
AppEntry apps[MAX_APPS];
int napps;
bool menu_open;
int menu_sel;
enum { POWER_NONE, POWER_REBOOT, POWER_OFF } pending_power;

uint32_t clipboard_len;
char* clipboard;
struct ClipOut { uint32_t pid; int shm; } clip_out[8];

char notice[96];
bool no_data_disk;

// ---- geometry ------------------------------------------------------------
Rect frame_rect(const Win& w) { return {w.x, w.y, w.cw + 2, w.ch + TITLE_H + 1}; }
Rect shadow_rect(const Win& w) {
    Rect f = frame_rect(w);
    return {f.x, f.y, f.w + SHADOW, f.h + SHADOW};
}
Rect content_rect(const Win& w) { return {w.x + 1, w.y + TITLE_H, w.cw, w.ch}; }
Rect button_rect(const Win& w, int i) { // 0 close, 1 maximize, 2 minimize
    Rect f = frame_rect(w);
    return {f.x + f.w - 1 - 36 * (i + 1), f.y + 1, 36, TITLE_H - 2};
}
Rect grip_rect(const Win& w) {
    Rect f = frame_rect(w);
    return {f.x + f.w - 14, f.y + f.h - 14, 14, 14};
}
Rect taskbar_rect() { return {0, SH - TASKBAR_H, SW, TASKBAR_H}; }
Rect launcher_button() { return {6, SH - TASKBAR_H + 5, 104, TASKBAR_H - 10}; }
Rect task_button(int slot) { return {118 + slot * 172, SH - TASKBAR_H + 5, 166, TASKBAR_H - 10}; }
Rect menu_rect() {
    int h = 16 + 30 + (napps + 2) * 36 + 12;
    return {6, SH - TASKBAR_H - 6 - h, 260, h};
}
Rect menu_item(int i) { // apps first, then the two power items
    Rect m = menu_rect();
    int y = m.y + 52 + i * 36 + (i >= napps ? 10 : 0);
    return {m.x + 8, y, m.w - 16, 34};
}
Rect clock_rect() { return {SW - 150, SH - TASKBAR_H, 150, TASKBAR_H}; }


// ---- window list helpers ---------------------------------------------------
int find_win(int id) {
    for (int i = 0; i < MAX_WIN; i++) if (wins[i].used && wins[i].id == id) return i;
    return -1;
}

void z_remove(int w) {
    int j = 0;
    for (int i = 0; i < nz; i++) if (zorder[i] != w) zorder[j++] = zorder[i];
    nz = j;
}

void send_to(int w, fih_msg_t& m) {
    m.a[0] = (uint32_t)wins[w].id;
    fih_msg_send((int)wins[w].owner, &m);
}

void send_focus(int w, bool on) {
    if (w < 0 || !wins[w].used) return;
    fih_msg_t m{};
    m.type = WS_EV_FOCUS;
    m.a[1] = on;
    send_to(w, m);
}

void set_focus(int w) {
    if (w == focus) return;
    int old = focus;
    focus = w;
    send_focus(old, false);
    send_focus(w, true);
    if (old >= 0) damage(frame_rect(wins[old]));
    if (w >= 0) damage(frame_rect(wins[w]));
    damage(taskbar_rect());
}

void raise(int w) {
    z_remove(w);
    zorder[nz++] = w;
    damage(shadow_rect(wins[w]));
}

void focus_topmost() {
    for (int i = nz - 1; i >= 0; i--)
        if (!wins[zorder[i]].minimized) { set_focus(zorder[i]); return; }
    set_focus(-1);
}

void activate(int w) {
    Win& win = wins[w];
    if (win.minimized) win.minimized = false;
    raise(w);
    set_focus(w);
}

void minimize(int w) {
    wins[w].minimized = true;
    damage(shadow_rect(wins[w]));
    damage(taskbar_rect());
    if (focus == w) {
        focus = -1;
        send_focus(w, false);
        focus_topmost();
    }
}

void request_size(int w, int cw, int ch) {
    fih_msg_t m{};
    m.type = WS_EV_RESIZE;
    m.a[1] = (uint32_t)cw;
    m.a[2] = (uint32_t)ch;
    send_to(w, m);
}

void set_geometry(int w, Rect frame) {
    Win& win = wins[w];
    damage(shadow_rect(win));
    win.x = frame.x;
    win.y = frame.y;
    win.cw = frame.w - 2;
    win.ch = frame.h - TITLE_H - 1;
    damage(shadow_rect(win));
    if (win.cw != win.bw || win.ch != win.bh) request_size(w, win.cw, win.ch);
}

void toggle_maximize(int w) {
    Win& win = wins[w];
    if (!(win.flags & WS_FLAG_RESIZABLE)) return;
    if (win.maximized) {
        win.maximized = false;
        set_geometry(w, win.restore);
    } else {
        win.restore = frame_rect(win);
        win.maximized = true;
        set_geometry(w, {0, 0, SW, SH - TASKBAR_H});
    }
}

void destroy_win(int w) {
    Win& win = wins[w];
    damage(shadow_rect(win));
    damage(taskbar_rect());
    if (win.shm) fih_shm_unmap(win.shm);
    z_remove(w);
    win.used = false;
    if (capture == w) capture = -1;
    if (drag_win == w) { drag = None; drag_win = -1; }
    if (focus == w) {
        focus = -1;
        focus_topmost();
    }
}

void close_request(int w) {
    fih_msg_t m{};
    m.type = WS_EV_CLOSE;
    send_to(w, m);
}

int window_at(int x, int y) {
    for (int i = nz - 1; i >= 0; i--) {
        const Win& w = wins[zorder[i]];
        if (!w.minimized && frame_rect(w).contains(x, y)) return zorder[i];
    }
    return -1;
}

// ---- drawing ---------------------------------------------------------------
void draw_fish(gfx::Canvas& c, int x, int y, uint32_t body, uint32_t tail) {
    // A small stylised fish: rounded body, triangular tail, one eye.
    c.fill_rounded({x + 5, y + 2, 15, 11}, 5, body);
    for (int i = 0; i < 6; i++) c.fill({x + i, y + 7 - i, 1, 2 * i + 1 > 11 ? 11 : 2 * i + 1}, tail);
    c.fill({x + 15, y + 5, 2, 2}, 0x0C1F28);
}

void draw_desktop(Rect r) {
    for (int y = r.y; y < r.y + r.h; y++) back.fill({r.x, y, r.w, 1}, desk_row[y]);
    if (nz == 0) {
        const char* hint = no_data_disk ? "No data disk attached: files cannot be saved."
                                        : "Click FishOS or press Ctrl+Esc to open an application.";
        int tw = gfx::text_width(font_sans, hint);
        back.text((SW - tw) / 2, (SH - TASKBAR_H) / 2 - 8, hint, 0x8DB3C2, font_sans);
    }
    if (no_data_disk && nz) back.text(16, 12, "No data disk: changes are not saved", 0xF2A58C, font_sans);
}

void draw_glyph_button(int i, Rect b, bool active, bool pressed) {
    uint32_t fg = active ? 0xFFFFFF : 0x51626B;
    if (pressed) back.fill(b, i == 0 ? theme::danger : gfx::mix(active ? theme::accent : 0xD9E0E3, 0, 50));
    int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
    if (i == 0) {
        for (int d = -4; d <= 4; d++) {
            back.fill({cx + d, cy + d, 2, 1}, fg);
            back.fill({cx + d, cy - d, 2, 1}, fg);
        }
    } else if (i == 1) {
        back.frame({cx - 5, cy - 4, 10, 9}, fg);
        back.hline(cx - 5, cy - 3, 10, fg);
    } else {
        back.fill({cx - 5, cy + 3, 10, 2}, fg);
    }
}

void draw_window(int w) {
    Win& win = wins[w];
    bool active = w == focus;
    Rect f = frame_rect(win);
    back.blend({f.x + SHADOW, f.y + f.h, f.w, SHADOW}, 0x000000, 60);
    back.blend({f.x + f.w, f.y + SHADOW, SHADOW, f.h - SHADOW}, 0x000000, 60);
    back.frame(f, active ? theme::accent_dark : 0x8A99A1);
    Rect title{f.x + 1, f.y + 1, f.w - 2, TITLE_H - 1};
    back.fill(title, active ? theme::accent : 0xD9E0E3);
    int bx = button_rect(win, 2).x;
    back.text(title.x + 12, title.y + (TITLE_H - font_sans.height) / 2, win.title, active ? 0xFFFFFF : 0x51626B,
              font_sans, bx - title.x - 20);
    for (int i = 0; i < 3; i++) {
        if (i == 1 && !(win.flags & WS_FLAG_RESIZABLE)) continue;
        bool pressed = drag == TitleButton && drag_win == w && title_button == i && button_rect(win, i).contains(mx, my);
        draw_glyph_button(i, button_rect(win, i), active, pressed);
    }
    Rect c = content_rect(win);
    Rect vis = c.intersect(back.clip);
    if (!vis.empty()) {
        back.blit(c.x, c.y, win.buf, win.bw, win.bh, win.bw, {0, 0, win.cw, win.ch});
        if (win.bw < win.cw) back.fill({c.x + win.bw, c.y, win.cw - win.bw, win.ch}, theme::bg);
        if (win.bh < win.ch) back.fill({c.x, c.y + win.bh, win.cw, win.ch - win.bh}, theme::bg);
    }
    if ((win.flags & WS_FLAG_RESIZABLE) && !win.maximized) {
        Rect g = grip_rect(win);
        for (int i = 0; i < 3; i++)
            for (int j = 0; j <= i; j++) back.fill({g.x + 10 - j * 4, g.y + 2 + i * 4, 2, 2}, 0x9AA8AF);
    }
}

void draw_taskbar() {
    Rect t = taskbar_rect();
    back.fill(t, BAR_BG);
    back.hline(0, t.y, SW, 0x1E3E4C);
    Rect lb = launcher_button();
    if (menu_open) back.fill_rounded(lb, 4, BAR_ACTIVE);
    draw_fish(back, lb.x + 10, lb.y + 8, theme::coral, 0xF7A98D);
    back.text(lb.x + 38, lb.y + (lb.h - font_sans.height) / 2, "FishOS", BAR_TEXT, font_sans);
    int slot = 0;
    for (int i = 0; i < MAX_WIN; i++) {
        const Win& w = wins[i];
        if (!w.used) continue;
        Rect b = task_button(slot++);
        if (b.x + b.w > clock_rect().x) break;
        bool active = i == focus && !w.minimized;
        back.fill_rounded(b, 4, active ? BAR_ACTIVE : 0x15303C);
        if (active) back.fill({b.x + 10, b.y + b.h - 3, b.w - 20, 2}, theme::coral);
        back.text(b.x + 10, b.y + (b.h - font_sans.height) / 2, w.title, w.minimized ? 0x87A0AB : BAR_TEXT,
                  font_sans, b.w - 20);
    }
    fih_time_t tm;
    fih_time(&tm);
    char clock[32];
    snprintf(clock, sizeof clock, "%02u:%02u", tm.hour, tm.minute);
    char date[32];
    snprintf(date, sizeof date, "%04u-%02u-%02u", tm.year, tm.month, tm.day);
    Rect cr = clock_rect();
    back.text(cr.x + cr.w - 16 - gfx::text_width(font_sans, clock), cr.y + 3, clock, BAR_TEXT, font_sans);
    back.text(cr.x + cr.w - 16 - gfx::text_width(font_sans, date), cr.y + 20, date, 0x87A0AB, font_sans);
    if (notice[0]) {
        int nw = gfx::text_width(font_sans, notice) + 24;
        Rect n{cr.x - nw - 8, t.y + 6, nw, t.h - 12};
        back.fill_rounded(n, 4, 0x5A3A1E);
        back.text(n.x + 12, n.y + (n.h - font_sans.height) / 2, notice, 0xFFD9C2, font_sans);
    }
}

void draw_menu() {
    Rect m = menu_rect();
    back.blend({m.x + SHADOW, m.y + SHADOW, m.w, m.h}, 0, 70);
    back.fill_rounded(m, 6, 0xF4F6F7);
    back.text(m.x + 16, m.y + 12, "Applications", theme::text_dim, font_sans);
    for (int i = 0; i < napps + 2; i++) {
        Rect it = menu_item(i);
        if (i == menu_sel) back.fill_rounded(it, 4, theme::select);
        if (i < napps) {
            ui::draw_icon(back, apps[i].icon, it.x + 8, it.y + 5, 24);
            back.text(it.x + 42, it.y + (it.h - font_sans.height) / 2, apps[i].name, theme::text, font_sans);
        } else {
            back.text(it.x + 42, it.y + (it.h - font_sans.height) / 2, i == napps ? "Restart" : "Shut down",
                      theme::text, font_sans);
        }
    }
    Rect sep = menu_item(napps);
    back.hline(m.x + 12, sep.y - 6, m.w - 24, theme::border);
}

// 12x19 arrow cursor: '#' outline, '.' fill.
const char* const CURSOR[19] = {
    "#           ", "##          ", "#.#         ", "#..#        ", "#...#       ", "#....#      ",
    "#.....#     ", "#......#    ", "#.......#   ", "#........#  ", "#.........# ", "#......#####",
    "#...#..#    ", "#..# #..#   ", "#.#  #..#   ", "##    #..#  ", "#     #..#  ", "       ##   ",
    "            "};
Rect cursor_rect() { return {mx, my, 12, 19}; }

void draw_cursor() {
    for (int y = 0; y < 19; y++)
        for (int x = 0; x < 12; x++) {
            char c = CURSOR[y][x];
            if (c == ' ') continue;
            back.fill({mx + x, my + y, 1, 1}, c == '#' ? 0x0B1E28 : 0xFFFFFF);
        }
}

void compose(Rect r) {
    back.clip = r;
    draw_desktop(r);
    for (int i = 0; i < nz; i++) {
        const Win& w = wins[zorder[i]];
        if (!w.minimized && !shadow_rect(w).intersect(r).empty()) draw_window(zorder[i]);
    }
    if (drag == Resize && !resize_outline.empty()) {
        back.frame(resize_outline, theme::coral);
        back.frame(resize_outline.inset(1), theme::coral);
    }
    if (!taskbar_rect().intersect(r).empty()) draw_taskbar();
    if (menu_open) draw_menu();
    draw_cursor();
    for (int y = r.y; y < r.y + r.h; y++)
        memcpy(screen.px + y * screen.stride + r.x, back.px + y * back.stride + r.x, (size_t)r.w * 4);
}

void flush() {
    for (int i = 0; i < ndirty; i++) compose(dirty[i]);
    ndirty = 0;
}

// ---- launcher --------------------------------------------------------------
ui::Icon icon_from(const char* name) {
    if (!strcmp(name, "files")) return ui::Icon::Files;
    if (!strcmp(name, "editor")) return ui::Icon::Editor;
    if (!strcmp(name, "terminal")) return ui::Icon::Terminal;
    if (!strcmp(name, "monitor")) return ui::Icon::Monitor;
    return ui::Icon::Program;
}

// Reads /system/apps/*.app descriptors ("name=", "exec=", "icon=" lines).
void load_apps() {
    napps = 0;
    int dir = fih_open("/system/apps", FIH_O_READ);
    if (dir < 0) return;
    fih_dirent_t d;
    for (uint32_t i = 0; napps < MAX_APPS && fih_readdir(dir, i, &d) == 1; i++) {
        size_t n = strlen(d.name);
        if (n < 5 || strcmp(d.name + n - 4, ".app") != 0) continue;
        char path[128], buf[512];
        snprintf(path, sizeof path, "/system/apps/%s", d.name);
        int fd = fih_open(path, FIH_O_READ);
        if (fd < 0) continue;
        int len = fih_read(fd, buf, sizeof buf - 1);
        fih_close(fd);
        if (len <= 0) continue;
        buf[len] = 0;
        AppEntry e{};
        char icon[24] = "";
        for (char* line = buf; line && *line;) {
            char* next = strchr(line, '\n');
            if (next) *next++ = 0;
            if (!strncmp(line, "name=", 5)) strlcpy(e.name, line + 5, sizeof e.name);
            else if (!strncmp(line, "exec=", 5)) strlcpy(e.exec, line + 5, sizeof e.exec);
            else if (!strncmp(line, "icon=", 5)) strlcpy(icon, line + 5, sizeof icon);
            line = next;
        }
        if (!e.name[0] || !e.exec[0]) continue;
        e.icon = icon_from(icon);
        // Keep the menu sorted by name.
        int pos = napps;
        while (pos > 0 && strcasecmp(apps[pos - 1].name, e.name) > 0) {
            apps[pos] = apps[pos - 1];
            pos--;
        }
        apps[pos] = e;
        napps++;
    }
    fih_close(dir);
}

void set_notice(const char* text) {
    strlcpy(notice, text, sizeof notice);
    damage(taskbar_rect());
}

void close_menu() {
    if (!menu_open) return;
    damage(menu_rect());
    menu_open = false;
    damage(taskbar_rect());
}

void open_menu() {
    load_apps();
    menu_open = true;
    menu_sel = 0;
    damage(menu_rect());
    damage(taskbar_rect());
}

void try_power();

void run_menu_item(int i) {
    close_menu();
    if (i < 0) return;
    if (i < napps) {
        int pid = ui::launch(apps[i].exec);
        if (pid < 0) {
            char msg[96];
            snprintf(msg, sizeof msg, "Cannot start %s: %s", apps[i].name, ui::error_string(pid));
            set_notice(msg);
        }
        return;
    }
    pending_power = i == napps ? POWER_REBOOT : POWER_OFF;
    // Ask every application to close so unsaved work can be handled first.
    for (int w = 0; w < MAX_WIN; w++) if (wins[w].used) close_request(w);
    set_notice(pending_power == POWER_REBOOT ? "Restarting when all windows are closed (click to cancel)"
                                             : "Shutting down when all windows are closed (click to cancel)");
    try_power();
}

void try_power() {
    if (!pending_power) return;
    for (int w = 0; w < MAX_WIN; w++) if (wins[w].used) return;
    fih_power(pending_power == POWER_REBOOT ? FIH_POWER_REBOOT : FIH_POWER_SHUTDOWN);
    pending_power = POWER_NONE;
    set_notice("Shut down is not supported on this machine. It is safe to power off.");
}

// ---- input -----------------------------------------------------------------
void send_mouse(int w, uint32_t kind, int clicks, int wheel) {
    const Win& win = wins[w];
    Rect c = content_rect(win);
    fih_msg_t m{};
    m.type = WS_EV_MOUSE;
    m.a[1] = kind;
    m.a[2] = (uint32_t)(mx - c.x);
    m.a[3] = (uint32_t)(my - c.y);
    m.a[4] = buttons;
    m.a[5] = (uint32_t)wheel;
    m.a[6] = (uint32_t)clicks;
    send_to(w, m);
}

int task_slot_at(int x, int y) {
    int slot = 0;
    for (int i = 0; i < MAX_WIN; i++) {
        if (!wins[i].used) continue;
        if (task_button(slot).contains(x, y)) return i;
        slot++;
    }
    return -1;
}

void mouse_down() {
    uint32_t now = fih_uptime();
    int dx = mx - last_click_x, dy = my - last_click_y;
    click_count = (now - last_click_time < 400 && dx * dx + dy * dy < 25) ? click_count + 1 : 1;
    last_click_time = now;
    last_click_x = mx;
    last_click_y = my;

    if (notice[0] && taskbar_rect().contains(mx, my) && mx < clock_rect().x && mx > 118 + 172 * nz) {
        notice[0] = 0;
        pending_power = POWER_NONE;
        damage(taskbar_rect());
        return;
    }
    if (menu_open) {
        if (menu_rect().contains(mx, my)) {
            for (int i = 0; i < napps + 2; i++)
                if (menu_item(i).contains(mx, my)) run_menu_item(i);
            return;
        }
        close_menu();
        if (launcher_button().contains(mx, my)) return;
    }
    if (taskbar_rect().contains(mx, my)) {
        if (launcher_button().contains(mx, my)) { open_menu(); return; }
        int w = task_slot_at(mx, my);
        if (w >= 0) {
            if (w == focus && !wins[w].minimized) minimize(w);
            else activate(w);
        }
        return;
    }
    int w = window_at(mx, my);
    if (w < 0) { set_focus(-1); return; }
    activate(w);
    Win& win = wins[w];
    Rect f = frame_rect(win);
    if (my < f.y + TITLE_H) {
        for (int i = 0; i < 3; i++) {
            if (i == 1 && !(win.flags & WS_FLAG_RESIZABLE)) continue;
            if (button_rect(win, i).contains(mx, my)) {
                drag = TitleButton;
                drag_win = w;
                title_button = i;
                damage(button_rect(win, i));
                return;
            }
        }
        if (click_count == 2) { toggle_maximize(w); return; }
        if (!win.maximized) {
            drag = Move;
            drag_win = w;
            drag_dx = mx - win.x;
            drag_dy = my - win.y;
        }
        return;
    }
    if ((win.flags & WS_FLAG_RESIZABLE) && !win.maximized && grip_rect(win).contains(mx, my)) {
        drag = Resize;
        drag_win = w;
        resize_outline = f;
        drag_dx = f.x + f.w - mx;
        drag_dy = f.y + f.h - my;
        return;
    }
    if (content_rect(win).contains(mx, my)) {
        capture = w;
        send_mouse(w, WS_MOUSE_DOWN, click_count, 0);
    }
}

void mouse_up() {
    if (drag == TitleButton) {
        int w = drag_win;
        drag = None;
        if (w >= 0 && wins[w].used) {
            damage(button_rect(wins[w], title_button));
            if (button_rect(wins[w], title_button).contains(mx, my)) {
                if (title_button == 0) close_request(w);
                else if (title_button == 1) toggle_maximize(w);
                else minimize(w);
            }
        }
    } else if (drag == Resize) {
        drag = None;
        damage(resize_outline.inset(-2));
        if (drag_win >= 0 && wins[drag_win].used) set_geometry(drag_win, resize_outline);
        resize_outline = {};
    } else if (drag == Move) {
        drag = None;
    }
    drag_win = -1;
    if (capture >= 0) {
        if (wins[capture].used) send_mouse(capture, WS_MOUSE_UP, 0, 0);
        capture = -1;
    }
}

void mouse_moved(int ox, int oy) {
    damage({ox, oy, 12, 19});
    damage(cursor_rect());
    if (drag == Move && drag_win >= 0) {
        Win& w = wins[drag_win];
        damage(shadow_rect(w));
        w.x = mx - drag_dx;
        w.y = my - drag_dy;
        if (w.y < 0) w.y = 0;
        if (w.y > SH - TASKBAR_H - TITLE_H) w.y = SH - TASKBAR_H - TITLE_H;
        if (w.x + frame_rect(w).w < 60) w.x = 60 - frame_rect(w).w;
        if (w.x > SW - 60) w.x = SW - 60;
        damage(shadow_rect(w));
    } else if (drag == Resize && drag_win >= 0) {
        damage(resize_outline.inset(-2));
        int nw = mx + drag_dx - resize_outline.x, nh = my + drag_dy - resize_outline.y;
        resize_outline.w = nw < MIN_W ? MIN_W : nw;
        resize_outline.h = nh < MIN_H ? MIN_H : nh;
        damage(resize_outline.inset(-2));
    } else if (drag == TitleButton && drag_win >= 0) {
        damage(button_rect(wins[drag_win], title_button));
    } else if (capture >= 0) {
        send_mouse(capture, WS_MOUSE_MOVE, 0, 0);
    }
}

void handle_mouse(const fih_msg_t& m) {
    int ox = mx, oy = my;
    mx += (int)m.a[0];
    my += (int)m.a[1];
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx > SW - 1) mx = SW - 1;
    if (my > SH - 1) my = SH - 1;
    uint32_t old = buttons;
    if (mx != ox || my != oy) {
        mouse_moved(ox, oy);
    }
    buttons = m.a[2] & 7;
    if ((buttons & FIH_MOUSE_LEFT) && !(old & FIH_MOUSE_LEFT)) mouse_down();
    if (!(buttons & FIH_MOUSE_LEFT) && (old & FIH_MOUSE_LEFT)) mouse_up();
    int wheel = (int)m.a[3];
    if (wheel) {
        int w = capture >= 0 ? capture : window_at(mx, my);
        if (w >= 0 && content_rect(wins[w]).contains(mx, my)) send_mouse(w, WS_MOUSE_WHEEL, 0, wheel);
    }
}

void cycle_windows() {
    if (nz == 0) return;
    // The bottom-most window becomes the active one: repeated Alt+Tab walks all of them.
    int w = zorder[0];
    if (nz > 1 && focus == zorder[nz - 1] && !wins[focus].minimized) w = zorder[0];
    activate(w);
}

void handle_key(const fih_msg_t& m) {
    uint32_t key = m.a[0], pressed = m.a[1], ch = m.a[2], mods = m.a[3];
    if (pressed) {
        if ((key == FIH_KEY_LSUPER || key == FIH_KEY_RSUPER) || (key == FIH_KEY_ESC && (mods & FIH_MOD_CTRL))) {
            if (menu_open) close_menu();
            else open_menu();
            return;
        }
        if (menu_open) {
            int n = napps + 2;
            if (key == FIH_KEY_ESC) close_menu();
            else if (key == FIH_KEY_UP) menu_sel = (menu_sel + n - 1) % n;
            else if (key == FIH_KEY_DOWN) menu_sel = (menu_sel + 1) % n;
            else if (key == FIH_KEY_ENTER) run_menu_item(menu_sel);
            damage(menu_rect());
            return;
        }
        if ((mods & FIH_MOD_ALT) && key == FIH_KEY_TAB) { cycle_windows(); return; }
        if ((mods & FIH_MOD_ALT) && key == FIH_KEY_F4) {
            if (focus >= 0) close_request(focus);
            return;
        }
    }
    if (focus >= 0 && !wins[focus].minimized) {
        fih_msg_t e{};
        e.type = WS_EV_KEY;
        e.a[1] = key;
        e.a[2] = pressed;
        e.a[3] = ch;
        e.a[4] = mods;
        send_to(focus, e);
    }
}

// ---- client requests ---------------------------------------------------------
uint32_t* map_buffer(int shm, int w, int h) {
    if (w < 1 || h < 1 || w > 4096 || h > 4096) return nullptr;
    int size = fih_shm_size(shm);
    if (size < w * h * 4) return nullptr; // never read past what the client provided
    return static_cast<uint32_t*>(fih_shm_map(shm));
}

void reply(uint32_t pid, uint32_t type, uint32_t a0, uint32_t a1) {
    fih_msg_t m{};
    m.type = type;
    m.a[0] = a0;
    m.a[1] = a1;
    fih_msg_send((int)pid, &m);
}

void handle_client(const fih_msg_t& m) {
    int w = (m.type >= WS_DESTROY && m.type <= WS_ACTIVATE) ? find_win((int)m.a[0]) : -1;
    if (w >= 0 && wins[w].owner != m.from) return; // only the owner may touch a window
    switch (m.type) {
    case WS_CREATE: {
        int slot = -1;
        for (int i = 0; i < MAX_WIN; i++) if (!wins[i].used) { slot = i; break; }
        int shm = (int)m.a[0], bw = (int)m.a[1], bh = (int)m.a[2];
        uint32_t* buf = slot >= 0 ? map_buffer(shm, bw, bh) : nullptr;
        if (!buf) {
            reply(m.from, WS_CREATED, m.a[4], (uint32_t)-FIH_EINVAL);
            return;
        }
        Win& win = wins[slot];
        win = Win{};
        win.used = true;
        win.id = next_id++;
        win.owner = m.from;
        win.shm = shm;
        win.buf = buf;
        win.bw = win.cw = bw;
        win.bh = win.ch = bh;
        win.flags = m.a[3];
        memcpy(win.title, &m.a[5], WS_TITLE_CREATE);
        win.title[WS_TITLE_CREATE] = 0;
        static int cascade;
        win.x = 70 + (cascade % 8) * 28;
        win.y = 36 + (cascade % 8) * 26;
        cascade++;
        if (win.cw + 2 > SW) win.cw = SW - 2;
        if (win.ch + TITLE_H + 1 > SH - TASKBAR_H) win.ch = SH - TASKBAR_H - TITLE_H - 1;
        if (win.x + win.cw + 2 > SW) win.x = SW - win.cw - 2;
        if (win.y + win.ch + TITLE_H + 1 > SH - TASKBAR_H) win.y = SH - TASKBAR_H - win.ch - TITLE_H - 1;
        if (win.x < 0) win.x = 0;
        if (win.y < 0) win.y = 0;
        zorder[nz++] = slot;
        reply(m.from, WS_CREATED, m.a[4], (uint32_t)win.id);
        damage(shadow_rect(win));
        damage(taskbar_rect());
        set_focus(slot);
        if (win.cw != win.bw || win.ch != win.bh) request_size(slot, win.cw, win.ch);
        break;
    }
    case WS_DESTROY:
        if (w >= 0) destroy_win(w);
        try_power();
        break;
    case WS_DAMAGE:
        if (w >= 0 && !wins[w].minimized) {
            Rect c = content_rect(wins[w]);
            Rect r{c.x + (int)m.a[1], c.y + (int)m.a[2], (int)m.a[3], (int)m.a[4]};
            damage(r.intersect(c));
        }
        break;
    case WS_SET_TITLE:
        if (w >= 0) {
            memcpy(wins[w].title, &m.a[1], sizeof(wins[w].title) - 1 < WS_TITLE_MAX ? sizeof(wins[w].title) - 1 : WS_TITLE_MAX);
            wins[w].title[sizeof(wins[w].title) - 1] = 0;
            damage(frame_rect(wins[w]));
            damage(taskbar_rect());
        }
        break;
    case WS_BUFFER:
        if (w >= 0) {
            int shm = (int)m.a[1], bw = (int)m.a[2], bh = (int)m.a[3];
            uint32_t* buf = map_buffer(shm, bw, bh);
            if (!buf) break;
            if (wins[w].shm != shm) fih_shm_unmap(wins[w].shm);
            wins[w].shm = shm;
            wins[w].buf = buf;
            wins[w].bw = bw;
            wins[w].bh = bh;
            damage(shadow_rect(wins[w]));
        }
        break;
    case WS_ACTIVATE:
        if (w >= 0) activate(w);
        break;
    case WS_CLIP_SET: {
        int shm = (int)m.a[0];
        uint32_t len = m.a[1];
        int size = fih_shm_size(shm);
        const char* src = (len <= WS_CLIP_MAX && size >= (int)len) ? static_cast<const char*>(fih_shm_map(shm)) : nullptr;
        char* copy = src ? static_cast<char*>(malloc(len + 1)) : nullptr;
        if (copy) {
            memcpy(copy, src, len);
            free(clipboard);
            clipboard = copy;
            clipboard_len = len;
        }
        if (src) fih_shm_unmap(shm);
        reply(m.from, WS_CLIP_ACK, copy ? 0 : (uint32_t)-FIH_EINVAL, 0);
        break;
    }
    case WS_CLIP_GET: {
        if (!clipboard || !clipboard_len) { reply(m.from, WS_CLIP_DATA, 0, 0); break; }
        int shm = fih_shm_create(clipboard_len);
        char* dst = shm > 0 ? static_cast<char*>(fih_shm_map(shm)) : nullptr;
        if (!dst || fih_shm_grant(shm, (int)m.from) < 0) {
            if (shm > 0) fih_shm_destroy(shm);
            reply(m.from, WS_CLIP_DATA, 0, 0);
            break;
        }
        memcpy(dst, clipboard, clipboard_len);
        fih_shm_unmap(shm);
        // Keep the object alive until the client says it copied the data.
        ClipOut* slot = &clip_out[0];
        for (auto& c : clip_out) if (!c.shm) { slot = &c; break; }
        if (slot->shm) fih_shm_destroy(slot->shm);
        *slot = {m.from, shm};
        reply(m.from, WS_CLIP_DATA, (uint32_t)shm, clipboard_len);
        break;
    }
    case WS_CLIP_DONE:
        for (auto& c : clip_out)
            if (c.shm == (int)m.a[0] && c.pid == m.from) {
                fih_shm_destroy(c.shm);
                c = {};
            }
        break;
    }
}

// Removes windows whose owning process is gone (crashed or exited without
// closing them) and releases clipboard transfers nobody picked up.
void reap_dead_clients() {
    fih_procinfo_t procs[64];
    int n = fih_proc_list(procs, 64);
    if (n < 0) return;
    auto live = [&](uint32_t pid) {
        for (int i = 0; i < n; i++) if (procs[i].pid == pid) return procs[i].state != FIH_PROC_ZOMBIE;
        return false;
    };
    for (int i = 0; i < MAX_WIN; i++) {
        if (wins[i].used && !live(wins[i].owner)) {
            char line[96];
            int len = snprintf(line, sizeof line, "desktop: removing window of exited pid %u", wins[i].owner);
            fih_log(line, (uint32_t)len);
            destroy_win(i);
        }
    }
    for (auto& c : clip_out)
        if (c.shm && !live(c.pid)) {
            fih_shm_destroy(c.shm);
            c = {};
        }
    try_power();
}
}

int main() {
    if (fih_display_acquire(&disp) < 0) {
        printf("desktop: cannot acquire the display\n");
        return 1;
    }
    if (fih_name_register(WS_SERVICE) < 0) {
        printf("desktop: another display server is running\n");
        return 1;
    }
    SW = (int)disp.width;
    SH = (int)disp.height;
    screen.reset(reinterpret_cast<uint32_t*>(disp.vaddr), SW, SH, (int)(disp.pitch / 4));
    auto* backpx = static_cast<uint32_t*>(malloc((size_t)SW * SH * 4));
    if (!backpx) {
        printf("desktop: out of memory for the back buffer\n");
        return 1;
    }
    back.reset(backpx, SW, SH, SW);
    for (int y = 0; y < SH && y < 2048; y++) desk_row[y] = gfx::mix(DESK_TOP, DESK_BOTTOM, (uint32_t)(y * 255 / SH));
    mx = SW / 2;
    my = SH / 2;
    fih_statfs_t sf;
    no_data_disk = fih_statfs("/", &sf) != 0 || !strcmp(sf.fstype, "none");
    load_apps();
    damage({0, 0, SW, SH});
    flush();
    printf("desktop: running at %dx%d, %d applications\n", SW, SH, napps);

    uint32_t last_clock = 99, last_reap = 0;
    for (;;) {
        fih_msg_t m;
        // Wait for input or client requests; wake up at least every second
        // for the clock and for noticing exited clients.
        int r = fih_msg_recv(&m, 1000);
        while (r == 1) {
            if (m.from == 0 && m.type == FIH_MSG_MOUSE) handle_mouse(m);
            else if (m.from == 0 && m.type == FIH_MSG_KEY) handle_key(m);
            else if (m.from == 0 && m.type == FIH_MSG_CHILD_EXIT) {
                fih_wait((int)m.a[0], nullptr, FIH_WNOHANG);
                last_reap = 0;
            } else if (m.from != 0) handle_client(m);
            r = fih_msg_recv(&m, 0); // drain everything before drawing
        }
        uint32_t now = fih_uptime();
        if (now - last_reap >= 1000) {
            reap_dead_clients();
            last_reap = now;
        }
        fih_time_t tm;
        if (fih_time(&tm) == 0 && tm.minute != last_clock) {
            last_clock = tm.minute;
            damage(clock_rect());
        }
        flush();
    }
}
