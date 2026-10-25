#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* memcpy(void* dst, const void* src, size_t n);
void* memmove(void* dst, const void* src, size_t n);
void* memset(void* dst, int c, size_t n);
int memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t n);
int strcasecmp(const char* a, const char* b);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
char* strstr(const char* h, const char* n);
size_t strlcpy(char* dst, const char* src, size_t size);
size_t strlcat(char* dst, const char* src, size_t size);
char* strdup(const char* s);
char* strndup(const char* s, size_t n);
#ifdef __cplusplus
}
#endif
