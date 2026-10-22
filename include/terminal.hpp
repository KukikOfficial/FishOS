#pragma once
#include <stdint.h>

void terminal_init();
void terminal_handle_char(char c);
void terminal_handle_backspace();
void terminal_handle_enter();
void terminal_draw(int x, int y, int w, int h);
void terminal_print(const char* text, uint32_t color = 0xFFFFFF);
void terminal_println(const char* text, uint32_t color = 0xFFFFFF);
