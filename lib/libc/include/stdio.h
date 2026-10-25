#pragma once
// No printf format attributes: on i686-elf uint32_t is "unsigned long"; the
// formatter treats %d/%u/%x (with or without l) as 32-bit values.
#include <stdarg.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap);
int snprintf(char* buf, size_t size, const char* fmt, ...);
int dprintf(int fd, const char* fmt, ...);
int printf(const char* fmt, ...);
int puts(const char* s);
#ifdef __cplusplus
}
#endif
