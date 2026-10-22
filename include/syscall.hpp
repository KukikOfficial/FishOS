#pragma once
#include <stdint.h>
#include "idt.hpp"

enum SyscallNumber {
    SYS_YIELD    = 0,
    SYS_EXIT     = 1,
    SYS_SLEEP    = 2,
    SYS_GETPID   = 3,
    SYS_GETUPTIME= 4,
    SYS_VFS_READ = 5,
    SYS_VFS_WRITE= 6,
    SYS_BEEP     = 7
};

void syscall_init();

// Userspace inline syscall helper wrappers
static inline uint32_t syscall0(uint32_t num) {
    uint32_t ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(num));
    return ret;
}

static inline uint32_t syscall1(uint32_t num, uint32_t arg1) {
    uint32_t ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(num), "b"(arg1));
    return ret;
}

static inline uint32_t syscall2(uint32_t num, uint32_t arg1, uint32_t arg2) {
    uint32_t ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(num), "b"(arg1), "c"(arg2));
    return ret;
}

static inline uint32_t syscall3(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    uint32_t ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(num), "b"(arg1), "c"(arg2), "d"(arg3));
    return ret;
}
