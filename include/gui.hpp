#pragma once
#include <stdint.h>

struct Window {
    int id; // 0 = Explorer, 1 = Notepad, 2 = Paint, 3 = System Monitor
    int x, y, w, h;
    int orig_x, orig_y, orig_w, orig_h;
    const char* title;
    bool closed;
    bool minimized;
    bool maximized;
};

constexpr int WINDOW_COUNT = 4;
extern Window windows[WINDOW_COUNT];
extern int z_order[WINDOW_COUNT];
extern bool start_menu_open;
extern int blink_timer;

void gui_init();
void gui_handle_input(bool lclick_pressed, bool lclick_released);
void gui_draw();
void gui_handle_char(char c);
void gui_handle_backspace();