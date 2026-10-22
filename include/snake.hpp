#pragma once
#include <stdint.h>
#include "drivers.hpp"

void snake_init();
void snake_handle_key(SpecialKey key);
void snake_update();
void snake_draw(int x, int y, int w, int h);
