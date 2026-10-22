#include "string.hpp"

extern "C" {

void* memset(void* dest, int c, size_t n) {
    uint8_t* p = reinterpret_cast<uint8_t*>(dest);
    uint8_t val = static_cast<uint8_t>(c);
    while (n--) {
        *p++ = val;
    }
    return dest;
}

void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = reinterpret_cast<uint8_t*>(dest);
    const uint8_t* s = reinterpret_cast<const uint8_t*>(src);
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void* memmove(void* dest, const void* src, size_t n) {
    uint8_t* d = reinterpret_cast<uint8_t*>(dest);
    const uint8_t* s = reinterpret_cast<const uint8_t*>(src);
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = reinterpret_cast<const uint8_t*>(s1);
    const uint8_t* p2 = reinterpret_cast<const uint8_t*>(s2);
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}

}
