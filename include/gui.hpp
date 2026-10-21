#pragma once
#include <stdint.h>

struct Window {
    int id; // 0 = Explorer, 1 = Notepad, 2 = Paint
    int x, y, w, h;
    int orig_x, orig_y, orig_w, orig_h;
    const char* title;
    bool closed;
    bool minimized;
    bool maximized;
};

extern Window windows[3];
extern int z_order[3];
extern bool start_menu_open;
extern int blink_timer;

void gui_init();
void gui_handle_input(bool lclick_pressed, bool lclick_released);
void gui_draw();
void gui_handle_char(char c);
void gui_handle_backspace();