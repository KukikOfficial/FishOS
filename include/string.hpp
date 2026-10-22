#pragma once
#include <stdint.h>
#include <stddef.h>

extern "C" {
void* memset(void* dest, int c, size_t n);
void* memcpy(void* dest, const void* src, size_t n);
void* memmove(void* dest, const void* src, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);
}

inline int kstrlen(const char* s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

inline void kstrcpy(char* dest, const char* src) {
    while ((*dest++ = *src++));
}

inline bool kstrcmp(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return false;
        a++; b++;
    }
    return (*a == *b);
}

inline void int_to_str(int n, char* buf) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    int i = 0;
    bool neg = (n < 0);
    if (neg) n = -n;
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    if (neg) buf[i++] = '-';
    buf[i] = '\0';
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i - 1 - j]; buf[i - 1 - j] = t;
    }
}