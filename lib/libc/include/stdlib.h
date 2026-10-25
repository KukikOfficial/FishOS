#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* malloc(size_t n);
void* calloc(size_t n, size_t size);
void* realloc(void* p, size_t n);
void free(void* p);
int atoi(const char* s);
long strtol(const char* s, char** end, int base);
static inline int abs(int v) { return v < 0 ? -v : v; }
void qsort(void* base, size_t n, size_t size, int (*cmp)(const void*, const void*));
__attribute__((noreturn)) void exit(int code);
#ifdef __cplusplus
}
#endif
