#include "timer.hpp"
#include "idt.hpp"
#include "io.hpp"

static volatile uint32_t timer_ticks = 0;
static uint32_t timer_frequency = 100;

// Weak/extern hook for multitasking scheduler
extern void task_tick(registers_t* regs) __attribute__((weak));
void task_tick(registers_t*) {}

static void timer_callback(registers_t* regs) {
    timer_ticks = timer_ticks + 1;
    task_tick(regs);
}

void timer_init(uint32_t frequency) {
    timer_frequency = frequency;
    register_interrupt_handler(32, timer_callback);

    uint32_t divisor = 1193180 / frequency;

    // Send command byte (0x36 = Channel 0, lobyte/hibyte, Mode 3 square wave generator)
    outb(0x43, 0x36);
    outb(0x40, static_cast<uint8_t>(divisor & 0xFF));
    outb(0x40, static_cast<uint8_t>((divisor >> 8) & 0xFF));
}

uint32_t get_tick_count() {
    return timer_ticks;
}

void sleep_ms(uint32_t ms) {
    uint32_t ticks_to_wait = (ms * timer_frequency) / 1000;
    if (ticks_to_wait == 0) ticks_to_wait = 1;
    uint32_t target = timer_ticks + ticks_to_wait;
    while (timer_ticks < target) {
        __asm__ volatile ("hlt");
    }
}
