#include "gui.hpp"
#include "graphics.hpp"
#include "vfs.hpp"
#include "drivers.hpp"
#include "string.hpp"
#include "io.hpp"
#include "memory.hpp"
#include "task.hpp"
#include "timer.hpp"
#include "syscall.hpp"
#include "pe.hpp"
#include "terminal.hpp"
#include "snake.hpp"
#include "theme.hpp"

Window windows[WINDOW_COUNT] = {
    { 0,  90,  30, 400, 350,  90,  30, 400, 350, "File Explorer [VFS]", false, false, false, 280, 200 },
    { 1, 510,  30, 490, 350, 510,  30, 490, 350, "Text Editor (Notepad)", false, false, false, 300, 200 },
    { 2,  90, 400, 340, 290,  90, 400, 340, 290, "LitePaint Studio", true, false, false, 320, 240 },
    { 3, 460, 400, 540, 290, 460, 400, 540, 290, "System & Tasks Monitor", true, false, false, 350, 220 },
    { 4, 180,  90, 620, 410, 180,  90, 620, 410, "Terminal [FishShell]", false, false, false, 400, 260 },
    { 5, 270, 110, 340, 310, 270, 110, 340, 310, "Snake Arcade Game", true, false, false, 320, 280 },
    { 6, 310, 230, 400, 160, 310, 230, 400, 160, "FishWine Message", true, false, false, 320, 140 }
};

int z_order[WINDOW_COUNT] = { 2, 3, 5, 6, 0, 1, 4 };
bool start_menu_open = false;
int blink_timer = 0;

// Window moving & resizing
static int dragging_window = -1;
static int drag_offset_x = 0;
static int drag_offset_y = 0;

static int resizing_window = -1;
static int resize_start_w = 0;
static int resize_start_h = 0;
static int resize_mouse_start_x = 0;
static int resize_mouse_start_y = 0;

// Desktop Icons
struct DesktopIcon {
    int x, y;
    const char* title;
    int target_window;
    uint32_t icon_color;
};

static DesktopIcon desktop_icons[6] = {
    { 18,  20, "Computer", 0, 0x3B82F6 },
    { 18,  94, "Terminal", 4, 0x0F172A },
    { 18, 168, "Notepad",  1, 0x10B981 },
    { 18, 242, "Snake",    5, 0x22C55E },
    { 18, 316, "Paint",    2, 0xF59E0B },
    { 18, 390, "Tasks",    3, 0x8B5CF6 }
};
static int selected_desktop_icon = -1;
static uint32_t last_icon_click_tick = 0;

// Right-click Context Menu
static bool context_menu_open = false;
static int context_menu_x = 0;
static int context_menu_y = 0;
static int context_menu_type = 0; // 0 = Desktop, 1 = File in explorer
static int context_menu_target_file = -1;

// Alt+Tab HUD
static int alt_tab_hud_timer = 0;

// Notepad state
static char notepad_buffer[4096] = "";
static int notepad_cursor = 0;
static char notepad_filename[64] = "/readme.txt";

// Paint state
constexpr int PAINT_W = 310;
constexpr int PAINT_H = 180;
static uint32_t paint_canvas[PAINT_W * PAINT_H];
static uint32_t palette[6] = { 0x000000, 0xEF4444, 0x22C55E, 0x3B82F6, 0xEAB308, 0xFFFFFF };
static int active_color_idx = 0;

static void clear_canvas() {
    for (int i = 0; i < PAINT_W * PAINT_H; i++) paint_canvas[i] = 0xFFFFFF;
}

static void toggle_maximize(Window& win) {
    audio_click();
    if (!win.maximized) {
        win.orig_x = win.x; win.orig_y = win.y;
        win.orig_w = win.w; win.orig_h = win.h;
        win.x = 0; win.y = 0;
        win.w = SCREEN_W; win.h = SCREEN_H - 32;
        win.maximized = true;
    } else {
        win.x = win.orig_x; win.y = win.orig_y;
        win.w = win.orig_w; win.h = win.orig_h;
        win.maximized = false;
    }
}

static void focus_window(int id) {
    if (id < 0 || id >= WINDOW_COUNT) return;
    windows[id].closed = false;
    windows[id].minimized = false;
    for (int k = 0; k < WINDOW_COUNT - 1; k++) {
        if (z_order[k] == id) {
            z_order[k] = z_order[k + 1];
            z_order[WINDOW_COUNT - 1] = id;
        }
    }
}

static void user_sample_worker() {
    while (true) {
        syscall2(SYS_BEEP, 750, 30);
        syscall1(SYS_SLEEP, 200);
    }
}

void gui_init() {
    theme_init();
    terminal_init();
    snake_init();
    clear_canvas();

    vfs_node_t* init_file = vfs_lookup("/readme.txt");
    if (init_file && init_file->content) {
        kstrcpy(notepad_filename, "/readme.txt");
        kstrcpy(notepad_buffer, init_file->content);
        notepad_cursor = kstrlen(notepad_buffer);
    }
}

void gui_handle_char(char c) {
    int active_id = z_order[WINDOW_COUNT - 1];
    if (windows[active_id].closed || windows[active_id].minimized) return;

    if (active_id == 4) {
        terminal_handle_char(c);
    } else if (active_id == 1) {
        int len = kstrlen(notepad_buffer);
        if (len < 4094) {
            for (int i = len; i >= notepad_cursor; i--) {
                notepad_buffer[i + 1] = notepad_buffer[i];
            }
            notepad_buffer[notepad_cursor++] = c;
        }
    }
}

void gui_handle_backspace() {
    int active_id = z_order[WINDOW_COUNT - 1];
    if (windows[active_id].closed || windows[active_id].minimized) return;

    if (active_id == 4) {
        terminal_handle_backspace();
    } else if (active_id == 1) {
        if (notepad_cursor > 0) {
            int len = kstrlen(notepad_buffer);
            for (int i = notepad_cursor - 1; i < len; i++) {
                notepad_buffer[i] = notepad_buffer[i + 1];
            }
            notepad_cursor--;
        }
    }
}

void gui_handle_special_key(SpecialKey key) {
    // Alt+Tab
    if (key == KEY_TAB && alt_pressed) {
        audio_click();
        int top = z_order[0];
        for (int i = 0; i < WINDOW_COUNT - 1; i++) {
            z_order[i] = z_order[i + 1];
        }
        z_order[WINDOW_COUNT - 1] = top;
        windows[top].closed = false;
        windows[top].minimized = false;
        alt_tab_hud_timer = 50;
        return;
    }

    int active_id = z_order[WINDOW_COUNT - 1];
    if (windows[active_id].closed || windows[active_id].minimized) return;

    // Snake game controls
    if (active_id == 5) {
        snake_handle_key(key);
        return;
    }

    // Notepad controls
    if (active_id == 1) {
        int len = kstrlen(notepad_buffer);
        switch (key) {
            case KEY_LEFT:
                if (notepad_cursor > 0) notepad_cursor--;
                break;
            case KEY_RIGHT:
                if (notepad_cursor < len) notepad_cursor++;
                break;
            case KEY_HOME:
                while (notepad_cursor > 0 && notepad_buffer[notepad_cursor - 1] != '\n') notepad_cursor--;
                break;
            case KEY_END:
                while (notepad_cursor < len && notepad_buffer[notepad_cursor] != '\n') notepad_cursor++;
                break;
            case KEY_DELETE:
                if (notepad_cursor < len) {
                    for (int i = notepad_cursor; i < len; i++) notepad_buffer[i] = notepad_buffer[i + 1];
                }
                break;
            case KEY_TAB:
                for (int s = 0; s < 4; s++) gui_handle_char(' ');
                break;
            case KEY_SAVE:
                vfs_write_file(notepad_filename, notepad_buffer, kstrlen(notepad_buffer));
                audio_beep(1046, 50);
                break;
            case KEY_UP: {
                int col = 0;
                int p = notepad_cursor;
                while (p > 0 && notepad_buffer[p - 1] != '\n') { col++; p--; }
                if (p > 0) {
                    p--;
                    int prev_end = p;
                    while (p > 0 && notepad_buffer[p - 1] != '\n') p--;
                    int prev_len = prev_end - p;
                    notepad_cursor = p + ((col < prev_len) ? col : prev_len);
                }
                break;
            }
            case KEY_DOWN: {
                int col = 0;
                int p = notepad_cursor;
                while (p > 0 && notepad_buffer[p - 1] != '\n') { col++; p--; }
                p = notepad_cursor;
                while (p < len && notepad_buffer[p] != '\n') p++;
                if (p < len) {
                    p++;
                    int next_start = p;
                    while (p < len && notepad_buffer[p] != '\n') p++;
                    int next_len = p - next_start;
                    notepad_cursor = next_start + ((col < next_len) ? col : next_len);
                }
                break;
            }
            default: break;
        }
    }
}

static void draw_desktop_icons() {
    for (int i = 0; i < 6; i++) {
        const DesktopIcon& icon = desktop_icons[i];
        bool is_sel = (selected_desktop_icon == i);

        if (is_sel) {
            fill_rect(icon.x - 4, icon.y - 4, 68, 64, 0x1E3A8A);
            draw_rect(icon.x - 4, icon.y - 4, 68, 64, 0x60A5FA);
        }

        fill_rect(icon.x + 12, icon.y, 36, 32, icon.icon_color);
        draw_rect(icon.x + 12, icon.y, 36, 32, 0xFFFFFF);

        // Icon glyph
        if (i == 0) { // Computer
            fill_rect(icon.x + 18, icon.y + 6, 24, 16, 0x0F172A);
            fill_rect(icon.x + 26, icon.y + 24, 8, 4, 0xFFFFFF);
        } else if (i == 1) { // Terminal
            fill_rect(icon.x + 16, icon.y + 4, 28, 24, 0x020617);
            draw_string(icon.x + 20, icon.y + 10, ">_", 0x22C55E);
        } else if (i == 2) { // Notepad
            fill_rect(icon.x + 18, icon.y + 6, 24, 20, 0xFFFFFF);
            fill_rect(icon.x + 22, icon.y + 10, 16, 2, 0x334155);
            fill_rect(icon.x + 22, icon.y + 15, 12, 2, 0x334155);
        } else if (i == 3) { // Snake
            fill_rect(icon.x + 18, icon.y + 8, 12, 12, 0x22C55E);
            fill_rect(icon.x + 30, icon.y + 8, 10, 10, 0x4ADE80);
            fill_rect(icon.x + 26, icon.y + 18, 8, 8, 0xEF4444);
        } else if (i == 4) { // Paint
            fill_rect(icon.x + 18, icon.y + 6, 10, 10, 0xEF4444);
            fill_rect(icon.x + 32, icon.y + 6, 10, 10, 0x3B82F6);
        } else { // Tasks
            fill_rect(icon.x + 18, icon.y + 6, 24, 20, 0x1E1B4B);
            fill_rect(icon.x + 22, icon.y + 14, 16, 4, 0x10B981);
        }

        draw_string(icon.x, icon.y + 36, icon.title, 0xFFFFFF);
    }
}

static void draw_window(const Window& win, bool active) {
    if (win.closed || win.minimized) return;

    if (!win.maximized) draw_shadow(win.x + 6, win.y + 6, win.w, win.h);
    fill_rect(win.x, win.y, win.w, win.h, current_theme.win_bg);
    draw_rect(win.x, win.y, win.w, win.h, current_theme.win_border);

    uint32_t c1 = active ? current_theme.title_active_c1 : current_theme.title_inactive_c1;
    uint32_t c2 = active ? current_theme.title_active_c2 : current_theme.title_inactive_c2;
    draw_gradient_v(win.x + 1, win.y + 1, win.w - 2, 24, c1, c2);
    draw_string(win.x + 8, win.y + 7, win.title, current_theme.title_text);

    int bx_close = win.x + win.w - 20;
    int bx_max   = win.x + win.w - 38;
    int bx_min   = win.x + win.w - 56;
    int by = win.y + 4;

    fill_rect(bx_close, by, 16, 16, 0xEF4444);
    draw_string(bx_close + 4, by + 4, "X", 0xFFFFFF);

    fill_rect(bx_max, by, 16, 16, 0x3B82F6);
    draw_rect(bx_max + 3, by + 3, 10, 10, 0xFFFFFF);

    fill_rect(bx_min, by, 16, 16, 0x64748B);
    fill_rect(bx_min + 3, by + 11, 10, 2, 0xFFFFFF);

    int cx = win.x + 12;
    int cy = win.y + 34;

    // Window 0: File Explorer
    if (win.id == 0) {
        vfs_node_t* cur = vfs_get_current_dir();
        draw_string(cx, cy, "Current Path: ", 0x64748B);
        draw_string(cx + 96, cy, (cur && cur->name[0]) ? cur->name : "/", 0x0F172A);
        cy += 18;

        for (int i = 0; i < vfs_file_count && i < 8; i++) {
            const VFSGuiItem& item = vfs_gui_list[i];
            bool is_cur = (!item.is_dir && kstrcmp(notepad_filename, item.name));
            int item_w = win.w - 78;

            fill_rect(cx, cy, item_w, 24, item.is_dir ? 0xFEF3C7 : (is_cur ? 0xDBEAFE : 0xFFFFFF));
            draw_rect(cx, cy, item_w, 24, item.is_dir ? 0xD97706 : (is_cur ? 0x3B82F6 : 0xCBD5E1));

            if (item.is_dir) {
                draw_string(cx + 8, cy + 7, "[DIR] ", 0xB45309);
                draw_string(cx + 48, cy + 7, item.name, 0x92400E);
            } else {
                draw_string(cx + 8, cy + 7, item.name, 0x0F172A);
                char sz_buf[16];
                int_to_str(item.size, sz_buf);
                draw_string(cx + item_w - 60, cy + 7, sz_buf, 0x64748B);
                draw_string(cx + item_w - 20, cy + 7, "B", 0x64748B);
            }

            int del_x = cx + item_w + 6;
            fill_rect(del_x, cy, 48, 24, 0xFEE2E2);
            draw_rect(del_x, cy, 48, 24, 0xEF4444);
            draw_string(del_x + 12, cy + 7, "Del", 0xDC2626);

            cy += 28;
        }

        int btn_y = win.y + win.h - 32;
        fill_rect(cx, btn_y, 100, 24, 0x10B981);
        draw_rect(cx, btn_y, 100, 24, 0x059669);
        draw_string(cx + 10, btn_y + 7, "+ New File", 0xFFFFFF);

        fill_rect(cx + 110, btn_y, 110, 24, 0xF59E0B);
        draw_rect(cx + 110, btn_y, 110, 24, 0xD97706);
        draw_string(cx + 120, btn_y + 7, "+ New Folder", 0xFFFFFF);
    }
    // Window 1: Notepad
    else if (win.id == 1) {
        draw_string(cx, cy + 4, "File: ", 0x64748B);
        draw_string(cx + 42, cy + 4, notepad_filename, 0x0F172A);

        int save_btn_x = win.x + win.w - 110;
        fill_rect(save_btn_x, cy, 98, 22, 0x2563EB);
        draw_rect(save_btn_x, cy, 98, 22, 0x1D4ED8);
        draw_string(save_btn_x + 8, cy + 6, "[Save Ctrl+S]", 0xFFFFFF);
        cy += 28;

        int edit_w = win.w - 24;
        int edit_h = win.h - 72;
        fill_rect(cx, cy, edit_w, edit_h, 0xFFFFFF);
        draw_rect(cx, cy, edit_w, edit_h, 0x94A3B8);

        int text_x = cx + 8;
        int text_y = cy + 8;
        int cur_x = text_x;
        int cur_y = text_y;
        int max_x = cx + edit_w - 20;
        int max_y = cy + edit_h - 14;

        for (int i = 0; notepad_buffer[i] != '\0'; i++) {
            if (i == notepad_cursor && (blink_timer % 40 < 20)) {
                if (cur_y <= max_y) fill_rect(cur_x, cur_y, 2, 10, 0x2563EB);
            }
            if (notepad_buffer[i] == '\n') {
                cur_x = text_x;
                cur_y += 12;
            } else {
                if (cur_y <= max_y) draw_char(cur_x, cur_y, notepad_buffer[i], 0x0F172A);
                cur_x += 8;
                if (cur_x >= max_x) {
                    cur_x = text_x;
                    cur_y += 12;
                }
            }
        }
        if (notepad_cursor == kstrlen(notepad_buffer) && (blink_timer % 40 < 20)) {
            if (cur_y <= max_y) fill_rect(cur_x, cur_y, 2, 10, 0x2563EB);
        }
    }
    // Window 2: Paint
    else if (win.id == 2) {
        int pal_x = cx;
        for (int i = 0; i < 6; i++) {
            fill_rect(pal_x, cy, 20, 20, palette[i]);
            draw_rect(pal_x, cy, 20, 20, (active_color_idx == i) ? 0x000000 : 0x94A3B8);
            if (active_color_idx == i) draw_rect(pal_x + 2, cy + 2, 16, 16, 0xFFFFFF);
            pal_x += 26;
        }

        int clr_x = win.x + win.w - 80;
        fill_rect(clr_x, cy, 68, 20, 0xE2E8F0);
        draw_rect(clr_x, cy, 68, 20, 0x94A3B8);
        draw_string(clr_x + 8, cy + 5, "Clear", 0x0F172A);
        cy += 26;

        int canvas_w = (win.w - 24 < PAINT_W) ? win.w - 24 : PAINT_W;
        int canvas_h = (win.h - 76 < PAINT_H) ? win.h - 76 : PAINT_H;
        draw_rect(cx - 1, cy - 1, canvas_w + 2, canvas_h + 2, 0x64748B);
        for (int py = 0; py < canvas_h; py++) {
            for (int px = 0; px < canvas_w; px++) {
                put_pixel(cx + px, cy + py, paint_canvas[py * PAINT_W + px]);
            }
        }
    }
    // Window 3: System & Tasks Monitor
    else if (win.id == 3) {
        draw_string(cx, cy, "Kernel: FishOS x86 Protected Mode (Ring 0 & Ring 3 Syscalls)", 0x1E40AF);
        cy += 18;

        uint32_t uptime_sec = get_tick_count() / 100;
        char up_buf[32] = "Uptime: ";
        char sec_buf[16];
        int_to_str(uptime_sec, sec_buf);
        int p = kstrlen(up_buf);
        for (int s = 0; sec_buf[s]; s++) up_buf[p++] = sec_buf[s];
        up_buf[p++] = 's'; up_buf[p] = '\0';
        draw_string(cx, cy, up_buf, 0x0F172A);

        uint32_t total_mb = pmm_get_total_memory() / (1024 * 1024);
        uint32_t free_mb = pmm_get_free_memory() / (1024 * 1024);
        char mem_buf[64] = "RAM: ";
        char num_buf[16];
        int_to_str(total_mb, num_buf);
        p = kstrlen(mem_buf);
        for (int s = 0; num_buf[s]; s++) mem_buf[p++] = num_buf[s];
        mem_buf[p++] = 'M'; mem_buf[p++] = 'B'; mem_buf[p++] = ' '; mem_buf[p++] = '(';
        int_to_str(free_mb, num_buf);
        for (int s = 0; num_buf[s]; s++) mem_buf[p++] = num_buf[s];
        mem_buf[p++] = 'M'; mem_buf[p++] = 'B'; mem_buf[p++] = ' '; mem_buf[p++] = 'f';
        mem_buf[p++] = 'r'; mem_buf[p++] = 'e'; mem_buf[p++] = 'e'; mem_buf[p++] = ')';
        mem_buf[p] = '\0';
        draw_string(cx + 180, cy, mem_buf, 0x059669);
        cy += 20;

        draw_string(cx, cy, "PID", 0x64748B);
        draw_string(cx + 40, cy, "Process Name", 0x64748B);
        draw_string(cx + 240, cy, "State", 0x64748B);
        draw_string(cx + 340, cy, "Privilege", 0x64748B);
        cy += 14;
        fill_rect(cx, cy, win.w - 24, 1, 0xCBD5E1);
        cy += 6;

        task_t* cur_task = task_get_list();
        if (cur_task) {
            task_t* t = cur_task;
            int count = 0;
            do {
                if (count++ >= 5) break;
                char pid_buf[16];
                int_to_str(t->id, pid_buf);
                draw_string(cx, cy, pid_buf, 0x0F172A);
                draw_string(cx + 40, cy, t->name, 0x0F172A);

                const char* st_name = "READY";
                if (t->state == TASK_RUNNING) st_name = "RUNNING";
                else if (t->state == TASK_SLEEPING) st_name = "SLEEPING";
                else if (t->state == TASK_TERMINATED) st_name = "EXITED";

                draw_string(cx + 240, cy, st_name, (t->state == TASK_RUNNING) ? 0x059669 : 0x64748B);
                draw_string(cx + 340, cy, t->is_user ? "Ring 3 (User)" : "Ring 0 (Kernel)", t->is_user ? 0x7C3AED : 0x0284C7);

                cy += 18;
                t = t->next;
            } while (t && t != cur_task);
        }

        int spawn_y = win.y + win.h - 34;
        fill_rect(cx, spawn_y, 220, 24, 0x7C3AED);
        draw_rect(cx, spawn_y, 220, 24, 0x6D28D9);
        draw_string(cx + 12, spawn_y + 7, "+ Spawn Ring 3 Task (Syscall)", 0xFFFFFF);
    }
    // Window 4: Terminal (FishShell)
    else if (win.id == 4) {
        terminal_draw(win.x + 8, win.y + 30, win.w - 16, win.h - 38);
    }
    // Window 5: Snake Arcade Game
    else if (win.id == 5) {
        snake_draw(win.x, win.y, win.w, win.h);
    }
    // Window 6: FishWine Win32 MessageBox
    else if (win.id == 6) {
        fill_rect(win.x + 16, win.y + 40, 36, 36, 0x2563EB);
        draw_rect(win.x + 16, win.y + 40, 36, 36, 0x60A5FA);
        draw_string(win.x + 30, win.y + 50, "i", 0xFFFFFF);

        draw_string(win.x + 64, win.y + 44, wine_get_msgbox_title(), 0x1E3A8A);
        draw_string(win.x + 64, win.y + 68, wine_get_msgbox_text(), 0x0F172A);

        int ok_x = win.x + (win.w - 80) / 2;
        int ok_y = win.y + win.h - 36;
        fill_rect(ok_x, ok_y, 80, 24, 0x2563EB);
        draw_rect(ok_x, ok_y, 80, 24, 0x1D4ED8);
        draw_string(ok_x + 30, ok_y + 7, "OK", 0xFFFFFF);
    }

    // Window Resize Grip
    if (!win.maximized && win.id != 6) {
        int rx = win.x + win.w - 2;
        int ry = win.y + win.h - 2;
        for (int d = 3; d <= 12; d += 4) {
            for (int p = 0; p < d; p++) {
                put_pixel(rx - p, ry - (d - p), 0x94A3B8);
            }
        }
    }
}

static void draw_context_menu() {
    if (!context_menu_open) return;

    int mw = 170;
    int mh = (context_menu_type == 0) ? 120 : 48;
    int mx = (context_menu_x + mw > SCREEN_W) ? SCREEN_W - mw : context_menu_x;
    int my = (context_menu_y + mh > SCREEN_H - 32) ? SCREEN_H - 32 - mh : context_menu_y;

    draw_shadow(mx + 4, my + 4, mw, mh);
    fill_rect(mx, my, mw, mh, 0x0F172A);
    draw_rect(mx, my, mw, mh, 0x38BDF8);

    if (context_menu_type == 0) {
        fill_rect(mx + 2, my + 2, mw - 4, 22, 0x1E293B);
        draw_string(mx + 10, my + 6, "Open Terminal", 0x38BDF8);

        fill_rect(mx + 2, my + 25, mw - 4, 22, 0x1E293B);
        draw_string(mx + 10, my + 29, "+ New Note", 0xFFFFFF);

        fill_rect(mx + 2, my + 48, mw - 4, 22, 0x1E293B);
        draw_string(mx + 10, my + 52, "Launch Snake", 0x4ADE80);

        fill_rect(mx + 2, my + 71, mw - 4, 22, 0x1E293B);
        draw_string(mx + 10, my + 75, "Play Audio Beep", 0xFFFFFF);

        fill_rect(mx + 2, my + 94, mw - 4, 22, 0x1E293B);
        draw_string(mx + 10, my + 98, "About FishOS", 0x38BDF8);
    } else {
        fill_rect(mx + 2, my + 2, mw - 4, 22, 0x1E293B);
        draw_string(mx + 10, my + 6, "Open File / EXE", 0xFFFFFF);

        fill_rect(mx + 2, my + 25, mw - 4, 22, 0xDC2626);
        draw_string(mx + 10, my + 29, "Delete Item", 0xFFFFFF);
    }
}

void gui_handle_input(bool lclick_pressed, bool lclick_released, bool rclick_pressed, bool rclick_released) {
    (void)rclick_released;

    // Open Wine message box if activated
    if (wine_is_msgbox_active() && windows[6].closed) {
        focus_window(6);
    }

    // Paint Drawing
    if (mouse_btn_left && z_order[WINDOW_COUNT - 1] == 2 && !windows[2].closed && !windows[2].minimized && resizing_window != 2) {
        int cx = windows[2].x + 12;
        int cy = windows[2].y + 60;
        int canvas_w = (windows[2].w - 24 < PAINT_W) ? windows[2].w - 24 : PAINT_W;
        int canvas_h = (windows[2].h - 76 < PAINT_H) ? windows[2].h - 76 : PAINT_H;
        if (mouse_x >= cx && mouse_x < cx + canvas_w && mouse_y >= cy && mouse_y < cy + canvas_h) {
            int px = mouse_x - cx;
            int py = mouse_y - cy;
            for (int dy = 0; dy <= 1; dy++) {
                for (int dx = 0; dx <= 1; dx++) {
                    if (px + dx < PAINT_W && py + dy < PAINT_H) {
                        paint_canvas[(py + dy) * PAINT_W + (px + dx)] = palette[active_color_idx];
                    }
                }
            }
        }
    }

    // Right-Click Context Menu
    if (rclick_pressed) {
        audio_click();
        context_menu_open = true;
        context_menu_x = mouse_x;
        context_menu_y = mouse_y;
        context_menu_type = 0;

        if (!windows[0].closed && !windows[0].minimized &&
            mouse_x >= windows[0].x && mouse_x < windows[0].x + windows[0].w &&
            mouse_y >= windows[0].y + 52 && mouse_y < windows[0].y + windows[0].h - 32) {
            int file_y = windows[0].y + 52;
            for (int f = 0; f < vfs_file_count && f < 8; f++) {
                if (mouse_y >= file_y && mouse_y <= file_y + 24) {
                    context_menu_type = 1;
                    context_menu_target_file = f;
                    break;
                }
                file_y += 28;
            }
        }
        return;
    }

    // Left Click
    if (lclick_pressed) {
        // Context Menu Click
        if (context_menu_open) {
            int mw = 170;
            int mh = (context_menu_type == 0) ? 120 : 48;
            int mx = (context_menu_x + mw > SCREEN_W) ? SCREEN_W - mw : context_menu_x;
            int my = (context_menu_y + mh > SCREEN_H - 32) ? SCREEN_H - 32 - mh : context_menu_y;

            if (mouse_x >= mx && mouse_x <= mx + mw && mouse_y >= my && mouse_y <= my + mh) {
                audio_click();
                if (context_menu_type == 0) {
                    if (mouse_y < my + 24) { // Open Terminal
                        focus_window(4);
                    } else if (mouse_y < my + 47) { // New Note
                        vfs_write_file("/quick_note.txt", "Created via right click!", 25);
                        kstrcpy(notepad_filename, "/quick_note.txt");
                        kstrcpy(notepad_buffer, "Created via right click!");
                        notepad_cursor = kstrlen(notepad_buffer);
                        focus_window(1);
                    } else if (mouse_y < my + 70) { // Snake
                        focus_window(5);
                    } else if (mouse_y < my + 93) { // Beep
                        audio_beep(880, 80);
                    } else { // About
                        wine_show_messagebox("FishOS v2.0 with FishWine\nx86 Protected Mode OS", "About FishOS");
                    }
                } else if (context_menu_type == 1 && context_menu_target_file >= 0 && context_menu_target_file < vfs_file_count) {
                    const VFSGuiItem& item = vfs_gui_list[context_menu_target_file];
                    if (mouse_y < my + 24) {
                        if (!item.is_dir && item.node_ptr && item.node_ptr->content) {
                            if (wine_load_pe(item.name)) {
                                focus_window(6);
                            } else {
                                kstrcpy(notepad_filename, item.name);
                                kstrcpy(notepad_buffer, item.node_ptr->content);
                                notepad_cursor = kstrlen(notepad_buffer);
                                focus_window(1);
                            }
                        }
                    } else {
                        if (item.node_ptr) vfs_delete_node(item.node_ptr);
                    }
                }
            }
            context_menu_open = false;
            return;
        }

        // Taskbar Clicks
        if (mouse_y >= SCREEN_H - 32) {
            audio_click();
            if (mouse_x >= 4 && mouse_x <= 84) {
                start_menu_open = !start_menu_open;
            } else {
                for (int i = 0; i < WINDOW_COUNT; i++) {
                    int tab_x = 94 + i * 115;
                    if (mouse_x >= tab_x && mouse_x <= tab_x + 105) {
                        if (windows[i].closed || windows[i].minimized) {
                            focus_window(i);
                        } else if (z_order[WINDOW_COUNT - 1] == i) {
                            windows[i].minimized = true;
                        } else {
                            focus_window(i);
                        }
                        break;
                    }
                }
                start_menu_open = false;
            }
        }
        // Start Menu Clicks
        else if (start_menu_open && mouse_x >= 4 && mouse_x <= 180 && mouse_y >= SCREEN_H - 260 && mouse_y < SCREEN_H - 32) {
            audio_click();
            if (mouse_y >= SCREEN_H - 56) {
                outb(0x64, 0xFE); // Reboot
            } else if (mouse_y >= SCREEN_H - 80) { // Theme Win95
                theme_set(THEME_WIN95);
            } else if (mouse_y >= SCREEN_H - 104) { // Theme Dark
                theme_set(THEME_DARK);
            } else if (mouse_y >= SCREEN_H - 128) { // Snake
                focus_window(5);
            } else if (mouse_y >= SCREEN_H - 152) { // Terminal
                focus_window(4);
            } else if (mouse_y >= SCREEN_H - 176) { // Tasks
                focus_window(3);
            } else if (mouse_y >= SCREEN_H - 200) { // Paint
                focus_window(2);
            } else if (mouse_y >= SCREEN_H - 224) { // Notepad
                focus_window(1);
            } else { // Explorer
                focus_window(0);
            }
            start_menu_open = false;
        }
        // Windows Interaction
        else {
            start_menu_open = false;
            bool clicked_window = false;

            for (int i = WINDOW_COUNT - 1; i >= 0; i--) {
                int w_idx = z_order[i];
                Window& win = windows[w_idx];
                if (win.closed || win.minimized) continue;

                if (mouse_x >= win.x && mouse_x < win.x + win.w &&
                    mouse_y >= win.y && mouse_y < win.y + win.h) {

                    focus_window(w_idx);

                    int bx_close = win.x + win.w - 20;
                    int bx_max   = win.x + win.w - 38;
                    int bx_min   = win.x + win.w - 56;
                    int by = win.y + 4;

                    if (mouse_x >= bx_close && mouse_x <= bx_close + 16 && mouse_y >= by && mouse_y <= by + 16) {
                        audio_click();
                        win.closed = true;
                        if (win.id == 6) wine_close_msgbox();
                    } else if (mouse_x >= bx_max && mouse_x <= bx_max + 16 && mouse_y >= by && mouse_y <= by + 16) {
                        toggle_maximize(win);
                    } else if (mouse_x >= bx_min && mouse_x <= bx_min + 16 && mouse_y >= by && mouse_y <= by + 16) {
                        audio_click();
                        win.minimized = true;
                    }
                    // Resize Grip
                    else if (!win.maximized && win.id != 6 && mouse_x >= win.x + win.w - 16 && mouse_y >= win.y + win.h - 16) {
                        resizing_window = w_idx;
                        resize_start_w = win.w;
                        resize_start_h = win.h;
                        resize_mouse_start_x = mouse_x;
                        resize_mouse_start_y = mouse_y;
                    }
                    // Title Dragging
                    else if (mouse_y <= win.y + 24) {
                        if (!win.maximized) {
                            dragging_window = w_idx;
                            drag_offset_x = mouse_x - win.x;
                            drag_offset_y = mouse_y - win.y;
                        }
                    }
                    // Explorer Clicks
                    else if (win.id == 0) {
                        int file_y = win.y + 52;
                        int item_w = win.w - 78;
                        for (int f = 0; f < vfs_file_count && f < 8; f++) {
                            const VFSGuiItem& item = vfs_gui_list[f];
                            if (mouse_x >= win.x + 12 && mouse_x <= win.x + 12 + item_w &&
                                mouse_y >= file_y && mouse_y <= file_y + 24) {
                                audio_click();
                                if (item.is_dir) {
                                    vfs_set_current_dir(item.node_ptr);
                                } else {
                                    // If Windows .exe: run via FishWine!
                                    int nlen = kstrlen(item.name);
                                    if (nlen > 4 && item.name[nlen-4]=='.' && item.name[nlen-3]=='e' && item.name[nlen-2]=='x' && item.name[nlen-1]=='e') {
                                        wine_load_pe(item.name);
                                        focus_window(6);
                                    } else {
                                        kstrcpy(notepad_filename, item.name);
                                        if (item.node_ptr && item.node_ptr->content) {
                                            kstrcpy(notepad_buffer, item.node_ptr->content);
                                        } else {
                                            notepad_buffer[0] = '\0';
                                        }
                                        notepad_cursor = kstrlen(notepad_buffer);
                                        focus_window(1);
                                    }
                                }
                                break;
                            }
                            int del_x = win.x + 12 + item_w + 6;
                            if (mouse_x >= del_x && mouse_x <= del_x + 48 &&
                                mouse_y >= file_y && mouse_y <= file_y + 24) {
                                audio_click();
                                if (item.node_ptr) vfs_delete_node(item.node_ptr);
                                break;
                            }
                            file_y += 28;
                        }

                        int btn_y = win.y + win.h - 32;
                        if (mouse_x >= win.x + 12 && mouse_x <= win.x + 112 &&
                            mouse_y >= btn_y && mouse_y <= btn_y + 24) {
                            audio_click();
                            char new_name[32] = "note";
                            char idx_str[8];
                            int_to_str(vfs_file_count + 1, idx_str);
                            int p = kstrlen(new_name);
                            for (int s = 0; idx_str[s]; s++) new_name[p++] = idx_str[s];
                            new_name[p++] = '.'; new_name[p++] = 't'; new_name[p++] = 'x'; new_name[p++] = 't'; new_name[p] = '\0';

                            vfs_write_file(new_name, "New text file.\nType here...", 27);
                            kstrcpy(notepad_filename, new_name);
                            kstrcpy(notepad_buffer, "New text file.\nType here...");
                            notepad_cursor = kstrlen(notepad_buffer);
                            focus_window(1);
                        } else if (mouse_x >= win.x + 122 && mouse_x <= win.x + 232 &&
                                   mouse_y >= btn_y && mouse_y <= btn_y + 24) {
                            audio_click();
                            char dir_name[32] = "dir_";
                            char idx_str[8];
                            int_to_str(vfs_file_count + 1, idx_str);
                            int p = kstrlen(dir_name);
                            for (int s = 0; idx_str[s]; s++) dir_name[p++] = idx_str[s];
                            dir_name[p] = '\0';
                            vfs_mkdir(dir_name);
                        }
                    }
                    // Notepad Save
                    else if (win.id == 1) {
                        int save_btn_x = win.x + win.w - 110;
                        if (mouse_x >= save_btn_x && mouse_x <= save_btn_x + 98 &&
                            mouse_y >= win.y + 34 && mouse_y <= win.y + 56) {
                            audio_beep(1046, 40);
                            vfs_write_file(notepad_filename, notepad_buffer, kstrlen(notepad_buffer));
                        }
                    }
                    // Paint
                    else if (win.id == 2) {
                        int pal_x = win.x + 12;
                        int pal_y = win.y + 34;
                        for (int p = 0; p < 6; p++) {
                            if (mouse_x >= pal_x && mouse_x <= pal_x + 20 &&
                                mouse_y >= pal_y && mouse_y <= pal_y + 20) {
                                audio_click();
                                active_color_idx = p;
                            }
                            pal_x += 26;
                        }
                        int clr_x = win.x + win.w - 80;
                        if (mouse_x >= clr_x && mouse_x <= clr_x + 68 &&
                            mouse_y >= pal_y && mouse_y <= pal_y + 20) {
                            audio_click();
                            clear_canvas();
                        }
                    }
                    // Tasks
                    else if (win.id == 3) {
                        int spawn_y = win.y + win.h - 34;
                        if (mouse_x >= win.x + 12 && mouse_x <= win.x + 232 &&
                            mouse_y >= spawn_y && mouse_y <= spawn_y + 24) {
                            audio_beep(880, 50);
                            task_create("UserBeep_Ring3", user_sample_worker, true);
                        }
                    }
                    // FishWine MsgBox OK button
                    else if (win.id == 6) {
                        int ok_x = win.x + (win.w - 80) / 2;
                        int ok_y = win.y + win.h - 36;
                        if (mouse_x >= ok_x && mouse_x <= ok_x + 80 && mouse_y >= ok_y && mouse_y <= ok_y + 24) {
                            audio_click();
                            wine_close_msgbox();
                            win.closed = true;
                        }
                    }

                    clicked_window = true;
                    break;
                }
            }

            // Desktop Icons Clicks
            if (!clicked_window) {
                selected_desktop_icon = -1;
                uint32_t now = get_tick_count();
                for (int i = 0; i < 6; i++) {
                    const DesktopIcon& icon = desktop_icons[i];
                    if (mouse_x >= icon.x - 4 && mouse_x <= icon.x + 64 &&
                        mouse_y >= icon.y - 4 && mouse_y <= icon.y + 64) {
                        audio_click();
                        if (selected_desktop_icon == i || (now - last_icon_click_tick < 50)) {
                            focus_window(icon.target_window);
                        }
                        selected_desktop_icon = i;
                        last_icon_click_tick = now;
                        break;
                    }
                }
            }
        }
    }

    // Dragging
    if (mouse_btn_left && dragging_window != -1) {
        int nx = mouse_x - drag_offset_x;
        int ny = mouse_y - drag_offset_y;
        if (ny < 0) ny = 0;
        if (ny > SCREEN_H - 60) ny = SCREEN_H - 60;
        windows[dragging_window].x = nx;
        windows[dragging_window].y = ny;
    }

    // Resizing
    if (mouse_btn_left && resizing_window != -1) {
        Window& win = windows[resizing_window];
        int nw = resize_start_w + (mouse_x - resize_mouse_start_x);
        int nh = resize_start_h + (mouse_y - resize_mouse_start_y);
        if (nw < win.min_w) nw = win.min_w;
        if (nh < win.min_h) nh = win.min_h;
        if (win.x + nw > SCREEN_W) nw = SCREEN_W - win.x;
        if (win.y + nh > SCREEN_H - 32) nh = SCREEN_H - 32 - win.y;
        win.w = nw;
        win.h = nh;
    }

    if (lclick_released) {
        dragging_window = -1;
        resizing_window = -1;
    }
}

void gui_draw() {
    // 1. Desktop background
    draw_gradient_v(0, 0, SCREEN_W, SCREEN_H - 32, current_theme.bg_top, current_theme.bg_bottom);
    for (int gy = 20; gy < SCREEN_H - 40; gy += 40) {
        for (int gx = 20; gx < SCREEN_W; gx += 40) {
            put_pixel(gx, gy, current_theme.grid_dots);
        }
    }

    draw_string(SCREEN_W - 180, SCREEN_H - 52, "FishOS v2.0 Desktop", current_theme.grid_dots);

    // 2. Desktop Icons
    draw_desktop_icons();

    // 3. Windows
    for (int i = 0; i < WINDOW_COUNT; i++) {
        draw_window(windows[z_order[i]], (i == WINDOW_COUNT - 1));
    }

    // 4. Taskbar
    fill_rect(0, SCREEN_H - 32, SCREEN_W, 32, current_theme.taskbar_bg);
    draw_rect(0, SCREEN_H - 32, SCREEN_W, 1, current_theme.taskbar_border);

    fill_rect(4, SCREEN_H - 28, 80, 24, start_menu_open ? 0x1E40AF : current_theme.start_btn_bg);
    draw_rect(4, SCREEN_H - 28, 80, 24, current_theme.start_btn_border);
    draw_string(20, SCREEN_H - 20, "Start", 0xFFFFFF);

    for (int i = 0; i < WINDOW_COUNT; i++) {
        int tab_x = 94 + i * 115;
        bool is_active_top = (z_order[WINDOW_COUNT - 1] == i && !windows[i].closed && !windows[i].minimized);
        fill_rect(tab_x, SCREEN_H - 28, 105, 24, is_active_top ? 0x2563EB : (windows[i].closed || windows[i].minimized ? 0x1E293B : 0x334155));
        draw_rect(tab_x, SCREEN_H - 28, 105, 24, is_active_top ? 0x60A5FA : 0x475569);

        char short_title[11];
        for (int s = 0; s < 10; s++) {
            short_title[s] = windows[i].title[s];
            if (!windows[i].title[s]) break;
        }
        short_title[10] = '\0';
        draw_string(tab_x + 6, SCREEN_H - 20, short_title, windows[i].closed ? 0x64748B : 0xFFFFFF);
    }

    int h = 0, m = 0, s = 0;
    get_rtc_time(h, m, s);
    char time_buf[16];
    time_buf[0] = '0' + (h / 10); time_buf[1] = '0' + (h % 10);
    time_buf[2] = ':';
    time_buf[3] = '0' + (m / 10); time_buf[4] = '0' + (m % 10);
    time_buf[5] = ':';
    time_buf[6] = '0' + (s / 10); time_buf[7] = '0' + (s % 10);
    time_buf[8] = '\0';
    draw_string(SCREEN_W - 80, SCREEN_H - 20, time_buf, current_theme.text_accent);

    // 5. Start Menu
    if (start_menu_open) {
        draw_shadow(4, SCREEN_H - 270, 175, 238);
        fill_rect(4, SCREEN_H - 270, 175, 238, 0x0F172A);
        draw_rect(4, SCREEN_H - 270, 175, 238, 0x38BDF8);

        fill_rect(6, SCREEN_H - 266, 171, 22, 0x1E293B);
        draw_string(14, SCREEN_H - 260, "File Explorer", 0xFFFFFF);

        fill_rect(6, SCREEN_H - 242, 171, 22, 0x1E293B);
        draw_string(14, SCREEN_H - 236, "Text Editor", 0xFFFFFF);

        fill_rect(6, SCREEN_H - 218, 171, 22, 0x1E293B);
        draw_string(14, SCREEN_H - 212, "Paint Studio", 0xFFFFFF);

        fill_rect(6, SCREEN_H - 194, 171, 22, 0x1E293B);
        draw_string(14, SCREEN_H - 188, "System Monitor", 0xFFFFFF);

        fill_rect(6, SCREEN_H - 170, 171, 22, 0x1E293B);
        draw_string(14, SCREEN_H - 164, "Terminal (Shell)", 0x38BDF8);

        fill_rect(6, SCREEN_H - 146, 171, 22, 0x1E293B);
        draw_string(14, SCREEN_H - 140, "Snake Arcade", 0x4ADE80);

        fill_rect(6, SCREEN_H - 122, 171, 22, 0x1E293B);
        draw_string(14, SCREEN_H - 116, "Theme: Modern Dark", 0x94A3B8);

        fill_rect(6, SCREEN_H - 98, 171, 22, 0x1E293B);
        draw_string(14, SCREEN_H - 92, "Theme: Win95 Classic", 0xFBBF24);

        fill_rect(6, SCREEN_H - 74, 171, 22, 0xDC2626);
        draw_string(14, SCREEN_H - 68, "[ Reboot PC ]", 0xFFFFFF);
    }

    // 6. Context Menu
    draw_context_menu();

    // 7. Alt+Tab HUD
    if (alt_tab_hud_timer > 0) {
        alt_tab_hud_timer--;
        int hw = 260, hh = 60;
        int hx = (SCREEN_W - hw) / 2;
        int hy = (SCREEN_H - hh) / 2;
        draw_shadow(hx + 4, hy + 4, hw, hh);
        fill_rect(hx, hy, hw, hh, 0x0F172A);
        draw_rect(hx, hy, hw, hh, 0x38BDF8);
        draw_string(hx + 16, hy + 14, "Active Window:", 0x94A3B8);
        draw_string(hx + 16, hy + 34, windows[z_order[WINDOW_COUNT - 1]].title, 0x38BDF8);
    }

    // 8. Mouse Cursor
    draw_cursor(mouse_x, mouse_y);
}