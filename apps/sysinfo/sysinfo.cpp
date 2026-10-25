// FishOS System Monitor: kernel, memory, storage and processes.
#include <fihcore/syscall.h>
#include <stdio.h>
#include <string.h>
#include "fishos/ui.h"

using gfx::Rect;
using gfx::font_sans;
using gfx::font_title;
using ui::Event;
namespace theme = ui::theme;

namespace {
ui::Window win;
ui::ListView list;
ui::Dialog dialog;
fih_sysinfo_t si;
fih_procinfo_t procs[64];
int nprocs;
int pressed = -1;
enum { DLG_KILL = 1, DLG_ERROR };

Rect kill_button() { return {win.w - 128, 196, 112, 28}; }

void sample() {
    fih_sysinfo(&si);
    int sel_pid = list.selected >= 0 && list.selected < nprocs ? (int)procs[list.selected].pid : -1;
    nprocs = fih_proc_list(procs, 64);
    if (nprocs < 0) nprocs = 0;
    list.count = nprocs;
    list.selected = -1;
    for (int i = 0; i < nprocs; i++) if ((int)procs[i].pid == sel_pid) list.selected = i;
    list.clamp();
}

void bar(gfx::Canvas& c, Rect r, uint32_t used, uint32_t total) {
    c.fill_rounded(r, 4, theme::panel);
    if (total) {
        int w = (int)((uint64_t)r.w * used / total);
        if (w > 0) c.fill_rounded({r.x, r.y, w < 8 ? 8 : w, r.h}, 4, used * 10 > total * 9 ? theme::coral : theme::accent);
    }
}

void draw() {
    gfx::Canvas& c = win.canvas;
    c.clip = {0, 0, win.w, win.h};
    c.fill({0, 0, win.w, win.h}, theme::bg);
    c.text(16, 12, "System Monitor", theme::text, font_title);
    char buf[160];
    uint32_t up = si.uptime_ms / 1000;
    snprintf(buf, sizeof buf, "%s  -  %s  -  up %uh %02um %02us", si.kernel, si.cpu, up / 3600, (up / 60) % 60, up % 60);
    c.text(16, 46, buf, theme::text_dim, font_sans, win.w - 32);

    int col = (win.w - 48) / 2;
    uint32_t used = si.mem_total_kb - si.mem_free_kb;
    snprintf(buf, sizeof buf, "Memory   %u / %u MiB", used / 1024, si.mem_total_kb / 1024);
    c.text(16, 76, buf, theme::text, font_sans);
    bar(c, {16, 98, col, 10}, used, si.mem_total_kb);
    snprintf(buf, sizeof buf, "Kernel heap   %u / %u KiB", si.heap_used_kb, si.heap_total_kb);
    c.text(32 + col, 76, buf, theme::text, font_sans);
    bar(c, {32 + col, 98, col, 10}, si.heap_used_kb, si.heap_total_kb);

    fih_statfs_t sf;
    if (fih_statfs("/", &sf) == 0 && sf.total_blocks) {
        char total[24], freeb[24];
        ui::format_size(total, sizeof total, sf.total_blocks * sf.block_size);
        ui::format_size(freeb, sizeof freeb, sf.free_blocks * sf.block_size);
        snprintf(buf, sizeof buf, "Data disk (%s on %s)   %s free of %s", sf.fstype, sf.device, freeb, total);
        c.text(16, 122, buf, theme::text, font_sans);
        bar(c, {16, 144, win.w - 32, 10}, sf.total_blocks - sf.free_blocks, sf.total_blocks);
    } else {
        c.text(16, 122, "No data disk mounted: files cannot be saved", theme::danger, font_sans);
    }

    snprintf(buf, sizeof buf, "Processes (%d)", nprocs);
    c.text(16, 200, buf, theme::text, font_sans);
    ui::draw_button(c, kill_button(), "End Process", ui::Danger, pressed == 0, list.selected < 0);
    Rect head{0, 232, win.w, 22};
    c.fill(head, theme::panel);
    const char* cols[] = {"PID", "Name", "State", "Memory", "CPU time"};
    int xs[] = {16, 80, win.w - 330, win.w - 220, win.w - 120};
    for (int i = 0; i < 5; i++) c.text(xs[i], head.y + 3, cols[i], theme::text_dim, font_sans);
    list.area = {0, head.y + head.h, win.w, win.h - head.y - head.h};
    c.fill(list.area, theme::field);
    c.clip = list.area;
    static const char* const states[] = {"?", "ready", "running", "waiting", "zombie"};
    for (int i = list.top; i < nprocs && i <= list.top + list.visible(); i++) {
        Rect r = list.row_rect(i);
        bool sel = i == list.selected;
        if (sel) c.fill(r, win.focused ? theme::select_strong : theme::select);
        uint32_t fg = sel && win.focused ? 0xFFFFFF : theme::text;
        const fih_procinfo_t& p = procs[i];
        snprintf(buf, sizeof buf, "%u", p.pid);
        c.text(xs[0], r.y + 4, buf, fg, font_sans);
        c.text(xs[1], r.y + 4, p.name, fg, font_sans, xs[2] - xs[1] - 8);
        c.text(xs[2], r.y + 4, states[p.state < 5 ? p.state : 0], fg, font_sans);
        snprintf(buf, sizeof buf, "%u KiB", p.mem_kb);
        c.text(xs[3], r.y + 4, buf, fg, font_sans);
        snprintf(buf, sizeof buf, "%u.%02u s", p.cpu_ms / 1000, (p.cpu_ms % 1000) / 10);
        c.text(xs[4], r.y + 4, buf, fg, font_sans);
    }
    c.clip = {0, 0, win.w, win.h};
    list.draw_scrollbar(c);
    dialog.draw(c);
    win.present();
}

void ask_kill() {
    if (list.selected < 0) return;
    const fih_procinfo_t& p = procs[list.selected];
    char msg[160];
    snprintf(msg, sizeof msg, "End \"%s\" (pid %u)? Unsaved work in it will be lost.", p.name, p.pid);
    dialog.confirm(DLG_KILL, "End Process", msg, "End Process", "Cancel");
    dialog.tag = DLG_KILL + 0x100 * (int)p.pid;
}
}

int main() {
    if (!win.open("System Monitor", 640, 520)) return 1;
    sample();
    draw();
    uint32_t last = fih_uptime();
    for (;;) {
        ui::Window* w;
        Event e;
        bool got = ui::next_event(&w, &e, 1000);
        if (fih_uptime() - last >= 1000) {
            sample();
            last = fih_uptime();
        }
        if (got) {
            if (e.type == Event::Close) { win.close(); return 0; }
            if (dialog.active()) {
                if (dialog.handle(e)) {
                    int pid = dialog.tag / 0x100, r = dialog.result;
                    dialog.close();
                    if (r == 0 && pid > 0) {
                        int err = fih_kill(pid);
                        if (err) dialog.message_box(DLG_ERROR, "End Process", ui::error_string(err));
                        sample();
                    }
                }
            } else if (e.type == Event::Key) {
                if (e.key == FIH_KEY_DELETE) ask_kill();
                else if (e.ctrl() && (e.ch | 32) == 'w') { win.close(); return 0; }
                else list.key(e);
            } else if (!list.mouse(e)) {
                if (e.type == Event::MouseDown) {
                    if (kill_button().contains(e.x, e.y)) pressed = 0;
                    int i = list.hit(e.x, e.y);
                    if (i >= 0) list.selected = i;
                } else if (e.type == Event::MouseUp) {
                    if (pressed == 0 && kill_button().contains(e.x, e.y)) ask_kill();
                    pressed = -1;
                }
            }
        }
        draw();
    }
}
