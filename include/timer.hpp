#pragma once
#include <stdint.h>

void timer_init(uint32_t frequency);
void sleep_ms(uint32_t ms);
uint32_t get_tick_count();
