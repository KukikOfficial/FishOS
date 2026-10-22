#pragma once
#include <stdint.h>
#include "drivers.hpp"

struct Window {
    int id; // 0 = Explorer, 1 = Notepad, 2 = Paint, 3 = System Monitor
    int x, y, w, h;
    int orig_x, orig_y, orig_w, orig_h;
    const char* title;
    bool closed;
    bool minimized;
    bool maximized;
    int min_w, min_h;
};

constexpr int WINDOW_COUNT = 7;
extern Window windows[WINDOW_COUNT];
extern int z_order[WINDOW_COUNT];
extern bool start_menu_open;
extern int blink_timer;

void gui_init();
void gui_handle_input(bool lclick_pressed, bool lclick_released, bool rclick_pressed, bool rclick_released);
void gui_draw();
void gui_handle_char(char c);
void gui_handle_backspace();
void gui_handle_special_key(SpecialKey key);