#pragma once
#include <stdint.h>
#include "idt.hpp"

enum TaskState {
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_TERMINATED
};

struct task_t {
    uint32_t id;
    char name[32];
    uint32_t esp;               // Current ESP during context switch
    uint32_t kernel_stack_top;  // Top of kernel stack for TSS.esp0
    uint32_t user_stack_top;    // Top of user stack
    TaskState state;
    uint32_t sleep_until_tick;
    bool is_user;
    task_t* next;
};

void task_init();
task_t* task_create(const char* name, void (*entry_point)(), bool is_user);
void task_yield();
void task_sleep(uint32_t ticks);
void task_exit();
task_t* task_get_current();
task_t* task_get_list();
int task_get_count();
void task_tick(registers_t* regs);
