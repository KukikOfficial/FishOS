#include "task.hpp"
#include "memory.hpp"
#include "string.hpp"
#include "gdt.hpp"
#include "timer.hpp"

extern "C" void context_switch(uint32_t* old_esp, uint32_t new_esp);
extern "C" void enter_user_mode(uint32_t entry_point, uint32_t user_stack);

static task_t* current_task = nullptr;
static task_t* task_list = nullptr;
static uint32_t next_task_id = 1;
static int total_tasks = 0;
static bool scheduler_enabled = false;

static void task_entry_wrapper(void (*func)(), bool is_user, uint32_t user_stack) {
    if (is_user) {
        enter_user_mode(reinterpret_cast<uint32_t>(func), user_stack);
    } else {
        func();
        task_exit();
    }
}

task_t* task_create(const char* name, void (*entry_point)(), bool is_user) {
    auto* task = static_cast<task_t*>(kmalloc(sizeof(task_t)));
    if (!task) return nullptr;
    memset(task, 0, sizeof(task_t));

    task->id = next_task_id++;
    kstrcpy(task->name, name);
    task->state = TASK_READY;
    task->is_user = is_user;

    // Allocate 4KB kernel stack
    uint32_t kstack = reinterpret_cast<uint32_t>(kmalloc(4096));
    task->kernel_stack_top = kstack + 4096;

    uint32_t ustack_top = 0;
    if (is_user) {
        uint32_t ustack = reinterpret_cast<uint32_t>(kmalloc(4096));
        ustack_top = ustack + 4096 - 16;
        task->user_stack_top = ustack_top;
    }

    // Set up stack for context_switch
    uint32_t* sp = reinterpret_cast<uint32_t*>(task->kernel_stack_top);

    // Arguments for task_entry_wrapper: user_stack, is_user, func
    *(--sp) = ustack_top;
    *(--sp) = is_user ? 1 : 0;
    *(--sp) = reinterpret_cast<uint32_t>(entry_point);

    // Dummy return address for task_entry_wrapper
    *(--sp) = 0;

    // Return address for context_switch -> task_entry_wrapper
    *(--sp) = reinterpret_cast<uint32_t>(task_entry_wrapper);

    // Callee-saved registers for context_switch (ebp, ebx, esi, edi)
    *(--sp) = 0; // ebp
    *(--sp) = 0; // ebx
    *(--sp) = 0; // esi
    *(--sp) = 0; // edi

    task->esp = reinterpret_cast<uint32_t>(sp);

    // Add to circular task list
    if (!task_list) {
        task_list = task;
        task->next = task;
    } else {
        task_t* cur = task_list;
        while (cur->next != task_list) cur = cur->next;
        cur->next = task;
        task->next = task_list;
    }

    total_tasks++;
    return task;
}

void task_init() {
    // Convert current thread into the main kernel task
    auto* main_task = static_cast<task_t*>(kmalloc(sizeof(task_t)));
    memset(main_task, 0, sizeof(task_t));
    main_task->id = 0;
    kstrcpy(main_task->name, "Kernel Main / GUI");
    main_task->state = TASK_RUNNING;
    main_task->is_user = false;

    // Set up main kernel stack
    main_task->kernel_stack_top = 0x90000; // Above initial stack
    main_task->next = main_task;

    task_list = main_task;
    current_task = main_task;
    total_tasks = 1;
    scheduler_enabled = true;

    set_kernel_stack(main_task->kernel_stack_top);
}

void task_yield() {
    if (!scheduler_enabled || !current_task) return;

    task_t* prev = current_task;
    task_t* next = prev->next;

    // Find next ready/running task
    uint32_t now = get_tick_count();
    while (next != prev) {
        if (next->state == TASK_SLEEPING && now >= next->sleep_until_tick) {
            next->state = TASK_READY;
        }
        if (next->state == TASK_READY || next->state == TASK_RUNNING) {
            break;
        }
        next = next->next;
    }

    if (next == prev) return; // No other runnable task

    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
    }
    next->state = TASK_RUNNING;
    current_task = next;

    set_kernel_stack(next->kernel_stack_top);
    context_switch(&prev->esp, next->esp);
}

void task_sleep(uint32_t ticks) {
    if (!current_task) return;
    current_task->state = TASK_SLEEPING;
    current_task->sleep_until_tick = get_tick_count() + ticks;
    task_yield();
}

void task_exit() {
    if (!current_task) return;
    current_task->state = TASK_TERMINATED;
    task_yield();
    while (true) { __asm__ volatile ("hlt"); }
}

task_t* task_get_current() {
    return current_task;
}

task_t* task_get_list() {
    return task_list;
}

int task_get_count() {
    return total_tasks;
}

void task_tick(registers_t*) {
    if (!scheduler_enabled || !current_task) return;

    // Check sleeping tasks
    uint32_t now = get_tick_count();
    task_t* t = task_list;
    if (t) {
        do {
            if (t->state == TASK_SLEEPING && now >= t->sleep_until_tick) {
                t->state = TASK_READY;
            }
            t = t->next;
        } while (t && t != task_list);
    }
}
