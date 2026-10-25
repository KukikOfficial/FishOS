#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>

void* memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = dst;
    const unsigned char* s = src;
    if ((((uintptr_t)d | (uintptr_t)s) & 3) == 0) {
        size_t w = n / 4;
        __asm__ volatile("rep movsl" : "+D"(d), "+S"(s), "+c"(w) : : "memory");
        n &= 3;
    }
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
    unsigned char* d = dst;
    const unsigned char* s = src;
    if (d == s || n == 0) return dst;
    if (d < s || d >= s + n) return memcpy(dst, src, n);
    while (n--) d[n] = s[n];
    return dst;
}

void* memset(void* dst, int c, size_t n) {
    unsigned char* d = dst;
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}

size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (a[i] != b[i] || !a[i]) return (unsigned char)a[i] - (unsigned char)b[i];
    return 0;
}

int strcasecmp(const char* a, const char* b) {
    while (*a && tolower(*a) == tolower(*b)) { a++; b++; }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

char* strchr(const char* s, int c) {
    for (;; s++) {
        if (*s == (char)c) return (char*)s;
        if (!*s) return 0;
    }
}

char* strrchr(const char* s, int c) {
    const char* r = 0;
    for (;; s++) {
        if (*s == (char)c) r = s;
        if (!*s) return (char*)r;
    }
}

char* strstr(const char* h, const char* n) {
    size_t len = strlen(n);
    if (!len) return (char*)h;
    for (; *h; h++)
        if (*h == *n && strncmp(h, n, len) == 0) return (char*)h;
    return 0;
}

size_t strlcpy(char* dst, const char* src, size_t size) {
    size_t len = strlen(src);
    if (size) {
        size_t n = len < size - 1 ? len : size - 1;
        memcpy(dst, src, n);
        dst[n] = 0;
    }
    return len;
}

size_t strlcat(char* dst, const char* src, size_t size) {
    size_t d = strlen(dst);
    if (d >= size) return d + strlen(src);
    return d + strlcpy(dst + d, src, size - d);
}

char* strndup(const char* s, size_t n) {
    size_t len = strlen(s);
    if (len > n) len = n;
    char* p = malloc(len + 1);
    if (!p) return 0;
    memcpy(p, s, len);
    p[len] = 0;
    return p;
}

char* strdup(const char* s) { return strndup(s, strlen(s)); }

int atoi(const char* s) { return (int)strtol(s, 0, 10); }

long strtol(const char* s, char** end, int base) {
    while (isspace(*s)) s++;
    int neg = 0;
    if (*s == '-' || *s == '+') neg = *s++ == '-';
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] | 32) == 'x') { s += 2; base = 16; }
    if (base == 0) base = 10;
    long v = 0;
    for (;; s++) {
        int d = isdigit(*s) ? *s - '0' : isalpha(*s) ? (*s | 32) - 'a' + 10 : 99;
        if (d >= base) break;
        v = v * base + d;
    }
    if (end) *end = (char*)s;
    return neg ? -v : v;
}

void qsort(void* base, size_t n, size_t size, int (*cmp)(const void*, const void*)) {
    // Shell sort: small, in place, no recursion; fine for directory listings.
    unsigned char* a = base;
    unsigned char tmp[256];
    if (size > sizeof(tmp)) return;
    for (size_t gap = n / 2; gap > 0; gap /= 2) {
        for (size_t i = gap; i < n; i++) {
            memcpy(tmp, a + i * size, size);
            size_t j = i;
            while (j >= gap && cmp(a + (j - gap) * size, tmp) > 0) {
                memcpy(a + j * size, a + (j - gap) * size, size);
                j -= gap;
            }
            memcpy(a + j * size, tmp, size);
        }
    }
}
