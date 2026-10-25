// Freestanding helpers shared by every kernel subsystem.
// No printf format attributes: on i686-elf uint32_t is "unsigned long", and
// the kernel formatter treats %d/%u/%x/%p uniformly as 32-bit values.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

extern "C" {
void* memcpy(void* dst, const void* src, size_t n);
void* memmove(void* dst, const void* src, size_t n);
void* memset(void* dst, int c, size_t n);
int memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t n);
}

// Copies at most size-1 bytes and always terminates. Returns strlen(src).
size_t strlcpy(char* dst, const char* src, size_t size);
int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap);
int snprintf(char* buf, size_t size, const char* fmt, ...);

// Kernel log: serial port always, boot console while it owns the screen.
void klog(const char* fmt, ...);
void klog_write(const char* s, size_t len);

struct Regs;
[[noreturn]] void panic(const char* fmt, ...);
[[noreturn]] void panic_regs(const Regs* r, const char* fmt, ...);

#define KASSERT(cond) \
    do { if (!(cond)) panic("assertion failed: %s (%s:%d)", #cond, __FILE__, __LINE__); } while (0)

template <typename T> static inline T kmin(T a, T b) { return a < b ? a : b; }
template <typename T> static inline T kmax(T a, T b) { return a > b ? a : b; }
static inline uint32_t align_up(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }
static inline uint32_t align_down(uint32_t v, uint32_t a) { return v & ~(a - 1); }
