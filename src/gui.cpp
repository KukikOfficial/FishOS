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

Window windows[WINDOW_COUNT] = {
    { 0,  30,  40, 380, 360, 30,  40, 380, 360, "File Explorer [Hierarchical VFS]", false, false, false },
    { 1, 430,  40, 560, 360, 430,  40, 560, 360, "Text Editor (Notepad)", false, false, false },
    { 2,  30, 420, 340, 290, 30, 420, 340, 290, "LitePaint Studio", false, false, false },
    { 3, 400, 420, 590, 290, 400, 420, 590, 290, "System & Tasks Monitor", false, false, false }
};

int z_order[WINDOW_COUNT] = { 0, 1, 2, 3 };
bool start_menu_open = false;
int blink_timer = 0;

static int dragging_window = -1;
static int drag_offset_x = 0;
static int drag_offset_y = 0;

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

// Sample User Mode Ring 3 Task
static void user_sample_worker() {
    while (true) {
        // Issue syscalls from Ring 3
        syscall2(SYS_BEEP, 750, 30);
        syscall1(SYS_SLEEP, 200); // sleep 2 seconds
    }
}

void gui_init() {
    clear_canvas();
    vfs_node_t* init_file = vfs_lookup("/readme.txt");
    if (init_file && init_file->content) {
        kstrcpy(notepad_filename, "/readme.txt");
        kstrcpy(notepad_buffer, init_file->content);
        notepad_cursor = kstrlen(notepad_buffer);
    }
}

void gui_handle_char(char c) {
    if (z_order[WINDOW_COUNT - 1] == 1 && !windows[1].closed && !windows[1].minimized) {
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
    if (z_order[WINDOW_COUNT - 1] == 1 && !windows[1].closed && !windows[1].minimized) {
        if (notepad_cursor > 0) {
            int len = kstrlen(notepad_buffer);
            for (int i = notepad_cursor - 1; i < len; i++) {
                notepad_buffer[i] = notepad_buffer[i + 1];
            }
            notepad_cursor--;
        }
    }
}

static void draw_window(const Window& win, bool active) {
    if (win.closed || win.minimized) return;

    if (!win.maximized) draw_shadow(win.x + 6, win.y + 6, win.w, win.h);
    fill_rect(win.x, win.y, win.w, win.h, 0xF8FAFC);
    draw_rect(win.x, win.y, win.w, win.h, 0x475569);

    draw_gradient_v(win.x + 1, win.y + 1, win.w - 2, 24, active ? 0x2563EB : 0x64748B, active ? 0x1E40AF : 0x334155);
    draw_string(win.x + 8, win.y + 7, win.title, 0xFFFFFF);

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
        // + New File button
        fill_rect(cx, btn_y, 100, 24, 0x10B981);
        draw_rect(cx, btn_y, 100, 24, 0x059669);
        draw_string(cx + 10, btn_y + 7, "+ New File", 0xFFFFFF);

        // + New Folder button
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
        draw_string(save_btn_x + 10, cy + 6, "[Save File]", 0xFFFFFF);
        cy += 28;

        int edit_w = win.w - 24;
        int edit_h = win.h - 72;
        fill_rect(cx, cy, edit_w, edit_h, 0xFFFFFF);
        draw_rect(cx, cy, edit_w, edit_h, 0x94A3B8);

        int text_x = cx + 8;
        int text_y = cy + 8;
        int cur_x = text_x;
        int cur_y = text_y;
        int max_x = cx + edit_w - 16;
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

        draw_rect(cx - 1, cy - 1, PAINT_W + 2, PAINT_H + 2, 0x64748B);
        for (int py = 0; py < PAINT_H; py++) {
            for (int px = 0; px < PAINT_W; px++) {
                put_pixel(cx + px, cy + py, paint_canvas[py * PAINT_W + px]);
            }
        }
    }
    // Window 3: System & Tasks Monitor
    else if (win.id == 3) {
        draw_string(cx, cy, "Kernel: FishOS x86 Protected Mode (Ring 0 & Ring 3 Syscalls)", 0x1E40AF);
        cy += 18;

        // Uptime and Memory stats
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

        // Task Table Header
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

        // Spawn User Task button
        int spawn_y = win.y + win.h - 34;
        fill_rect(cx, spawn_y, 220, 24, 0x7C3AED);
        draw_rect(cx, spawn_y, 220, 24, 0x6D28D9);
        draw_string(cx + 12, spawn_y + 7, "+ Spawn Ring 3 Task (Syscall)", 0xFFFFFF);
    }
}

void gui_handle_input(bool lclick_pressed, bool lclick_released) {
    if (mouse_btn_left && z_order[WINDOW_COUNT - 1] == 2 && !windows[2].closed && !windows[2].minimized) {
        int cx = windows[2].x + 12;
        int cy = windows[2].y + 60;
        if (mouse_x >= cx && mouse_x < cx + PAINT_W && mouse_y >= cy && mouse_y < cy + PAINT_H) {
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

    if (lclick_pressed) {
        // Taskbar click
        if (mouse_y >= SCREEN_H - 32) {
            if (mouse_x >= 4 && mouse_x <= 84) {
                start_menu_open = !start_menu_open;
            } else {
                for (int i = 0; i < WINDOW_COUNT; i++) {
                    int tab_x = 94 + i * 135;
                    if (mouse_x >= tab_x && mouse_x <= tab_x + 125) {
                        if (windows[i].closed) {
                            windows[i].closed = false; windows[i].minimized = false;
                            for (int k = 0; k < WINDOW_COUNT - 1; k++) if (z_order[k] == i) { z_order[k] = z_order[k+1]; z_order[WINDOW_COUNT - 1] = i; }
                        } else if (windows[i].minimized) {
                            windows[i].minimized = false;
                            for (int k = 0; k < WINDOW_COUNT - 1; k++) if (z_order[k] == i) { z_order[k] = z_order[k+1]; z_order[WINDOW_COUNT - 1] = i; }
                        } else if (z_order[WINDOW_COUNT - 1] == i) {
                            windows[i].minimized = true;
                        } else {
                            for (int k = 0; k < WINDOW_COUNT - 1; k++) if (z_order[k] == i) { z_order[k] = z_order[k+1]; z_order[WINDOW_COUNT - 1] = i; }
                        }
                        break;
                    }
                }
                start_menu_open = false;
            }
        }
        // Start Menu click
        else if (start_menu_open && mouse_x >= 4 && mouse_x <= 180 && mouse_y >= SCREEN_H - 180 && mouse_y < SCREEN_H - 32) {
            if (mouse_y >= SCREEN_H - 56) {
                outb(0x64, 0xFE); // Reboot
            } else if (mouse_y >= SCREEN_H - 86) {
                windows[3].closed = false; windows[3].minimized = false; z_order[WINDOW_COUNT - 1] = 3;
            } else if (mouse_y >= SCREEN_H - 116) {
                windows[2].closed = false; windows[2].minimized = false; z_order[WINDOW_COUNT - 1] = 2;
            } else if (mouse_y >= SCREEN_H - 146) {
                windows[1].closed = false; windows[1].minimized = false; z_order[WINDOW_COUNT - 1] = 1;
            } else {
                windows[0].closed = false; windows[0].minimized = false; z_order[WINDOW_COUNT - 1] = 0;
            }
            start_menu_open = false;
        } else {
            start_menu_open = false;
            bool handled = false;
            for (int i = WINDOW_COUNT - 1; i >= 0; i--) {
                int w_idx = z_order[i];
                Window& win = windows[w_idx];
                if (win.closed || win.minimized) continue;

                if (mouse_x >= win.x && mouse_x < win.x + win.w &&
                    mouse_y >= win.y && mouse_y < win.y + win.h) {

                    for (int k = i; k < WINDOW_COUNT - 1; k++) z_order[k] = z_order[k + 1];
                    z_order[WINDOW_COUNT - 1] = w_idx;

                    int bx_close = win.x + win.w - 20;
                    int bx_max   = win.x + win.w - 38;
                    int bx_min   = win.x + win.w - 56;
                    int by = win.y + 4;

                    if (mouse_x >= bx_close && mouse_x <= bx_close + 16 && mouse_y >= by && mouse_y <= by + 16) {
                        win.closed = true;
                    } else if (mouse_x >= bx_max && mouse_x <= bx_max + 16 && mouse_y >= by && mouse_y <= by + 16) {
                        toggle_maximize(win);
                    } else if (mouse_x >= bx_min && mouse_x <= bx_min + 16 && mouse_y >= by && mouse_y <= by + 16) {
                        win.minimized = true;
                    } else if (mouse_y <= win.y + 24) {
                        if (!win.maximized) {
                            dragging_window = w_idx;
                            drag_offset_x = mouse_x - win.x;
                            drag_offset_y = mouse_y - win.y;
                        }
                    }
                    // Window 0: File Explorer
                    else if (win.id == 0) {
                        int file_y = win.y + 52;
                        int item_w = win.w - 78;
                        for (int f = 0; f < vfs_file_count && f < 8; f++) {
                            const VFSGuiItem& item = vfs_gui_list[f];
                            if (mouse_x >= win.x + 12 && mouse_x <= win.x + 12 + item_w &&
                                mouse_y >= file_y && mouse_y <= file_y + 24) {
                                if (item.is_dir) {
                                    vfs_set_current_dir(item.node_ptr);
                                } else {
                                    kstrcpy(notepad_filename, item.name);
                                    if (item.node_ptr && item.node_ptr->content) {
                                        kstrcpy(notepad_buffer, item.node_ptr->content);
                                    } else {
                                        notepad_buffer[0] = '\0';
                                    }
                                    notepad_cursor = kstrlen(notepad_buffer);
                                    windows[1].closed = false; windows[1].minimized = false;
                                }
                                break;
                            }
                            int del_x = win.x + 12 + item_w + 6;
                            if (mouse_x >= del_x && mouse_x <= del_x + 48 &&
                                mouse_y >= file_y && mouse_y <= file_y + 24) {
                                if (item.node_ptr) {
                                    vfs_delete_node(item.node_ptr);
                                }
                                break;
                            }
                            file_y += 28;
                        }

                        int btn_y = win.y + win.h - 32;
                        // + New File click
                        if (mouse_x >= win.x + 12 && mouse_x <= win.x + 112 &&
                            mouse_y >= btn_y && mouse_y <= btn_y + 24) {
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
                            windows[1].closed = false; windows[1].minimized = false;
                        }
                        // + New Folder click
                        else if (mouse_x >= win.x + 122 && mouse_x <= win.x + 232 &&
                                 mouse_y >= btn_y && mouse_y <= btn_y + 24) {
                            char dir_name[32] = "dir_";
                            char idx_str[8];
                            int_to_str(vfs_file_count + 1, idx_str);
                            int p = kstrlen(dir_name);
                            for (int s = 0; idx_str[s]; s++) dir_name[p++] = idx_str[s];
                            dir_name[p] = '\0';
                            vfs_mkdir(dir_name);
                        }
                    }
                    // Window 1: Notepad
                    else if (win.id == 1) {
                        int save_btn_x = win.x + win.w - 110;
                        if (mouse_x >= save_btn_x && mouse_x <= save_btn_x + 98 &&
                            mouse_y >= win.y + 34 && mouse_y <= win.y + 56) {
                            vfs_write_file(notepad_filename, notepad_buffer, kstrlen(notepad_buffer));
                        }
                    }
                    // Window 2: Paint
                    else if (win.id == 2) {
                        int pal_x = win.x + 12;
                        int pal_y = win.y + 34;
                        for (int p = 0; p < 6; p++) {
                            if (mouse_x >= pal_x && mouse_x <= pal_x + 20 &&
                                mouse_y >= pal_y && mouse_y <= pal_y + 20) {
                                active_color_idx = p;
                            }
                            pal_x += 26;
                        }
                        int clr_x = win.x + win.w - 80;
                        if (mouse_x >= clr_x && mouse_x <= clr_x + 68 &&
                            mouse_y >= pal_y && mouse_y <= pal_y + 20) {
                            clear_canvas();
                        }
                    }
                    // Window 3: System & Tasks Monitor
                    else if (win.id == 3) {
                        int spawn_y = win.y + win.h - 34;
                        if (mouse_x >= win.x + 12 && mouse_x <= win.x + 232 &&
                            mouse_y >= spawn_y && mouse_y <= spawn_y + 24) {
                            task_create("UserBeep_Ring3", user_sample_worker, true);
                        }
                    }
                    handled = true;
                    break;
                }
            }
            if (!handled) dragging_window = -1;
        }
    }

    if (mouse_btn_left && dragging_window != -1) {
        windows[dragging_window].x = mouse_x - drag_offset_x;
        windows[dragging_window].y = mouse_y - drag_offset_y;
    }
    if (lclick_released) dragging_window = -1;
}

void gui_draw() {
    draw_gradient_v(0, 0, SCREEN_W, SCREEN_H - 32, 0x0F172A, 0x1E293B);

    for (int i = 0; i < WINDOW_COUNT; i++) {
        draw_window(windows[z_order[i]], (i == WINDOW_COUNT - 1));
    }

    fill_rect(0, SCREEN_H - 32, SCREEN_W, 32, 0x0F172A);
    draw_rect(0, SCREEN_H - 32, SCREEN_W, 1, 0x334155);

    fill_rect(4, SCREEN_H - 28, 80, 24, start_menu_open ? 0x1E40AF : 0x2563EB);
    draw_rect(4, SCREEN_H - 28, 80, 24, 0x38BDF8);
    draw_string(20, SCREEN_H - 20, "Start", 0xFFFFFF);

    for (int i = 0; i < WINDOW_COUNT; i++) {
        int tab_x = 94 + i * 135;
        bool is_active_top = (z_order[WINDOW_COUNT - 1] == i && !windows[i].closed && !windows[i].minimized);
        fill_rect(tab_x, SCREEN_H - 28, 125, 24, is_active_top ? 0x2563EB : (windows[i].closed || windows[i].minimized ? 0x1E293B : 0x334155));
        draw_rect(tab_x, SCREEN_H - 28, 125, 24, is_active_top ? 0x60A5FA : 0x475569);

        char short_title[13];
        for (int s = 0; s < 12; s++) {
            short_title[s] = windows[i].title[s];
            if (!windows[i].title[s]) break;
        }
        short_title[12] = '\0';
        draw_string(tab_x + 8, SCREEN_H - 20, short_title, windows[i].closed ? 0x64748B : 0xFFFFFF);
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
    draw_string(SCREEN_W - 80, SCREEN_H - 20, time_buf, 0x38BDF8);

    if (start_menu_open) {
        draw_shadow(4, SCREEN_H - 180, 175, 148);
        fill_rect(4, SCREEN_H - 180, 175, 148, 0x0F172A);
        draw_rect(4, SCREEN_H - 180, 175, 148, 0x38BDF8);

        fill_rect(6, SCREEN_H - 176, 171, 24, 0x1E293B);
        draw_string(14, SCREEN_H - 168, "File Explorer [VFS]", 0xFFFFFF);

        fill_rect(6, SCREEN_H - 146, 171, 24, 0x1E293B);
        draw_string(14, SCREEN_H - 138, "Text Editor", 0xFFFFFF);

        fill_rect(6, SCREEN_H - 116, 171, 24, 0x1E293B);
        draw_string(14, SCREEN_H - 108, "Paint Studio", 0xFFFFFF);

        fill_rect(6, SCREEN_H - 86, 171, 24, 0x1E293B);
        draw_string(14, SCREEN_H - 78, "System Monitor", 0xFFFFFF);

        fill_rect(6, SCREEN_H - 56, 171, 24, 0xDC2626);
        draw_string(14, SCREEN_H - 48, "[ Reboot PC ]", 0xFFFFFF);
    }

    draw_cursor(mouse_x, mouse_y);
}