#include "lib.hpp"

extern "C" void* memcpy(void* dst, const void* src, size_t n) {
    auto* d = static_cast<uint8_t*>(dst);
    auto* s = static_cast<const uint8_t*>(src);
    if (((uintptr_t)d & 3) == 0 && ((uintptr_t)s & 3) == 0) {
        size_t words = n / 4;
        asm volatile("rep movsl" : "+D"(d), "+S"(s), "+c"(words) : : "memory");
        n &= 3;
    }
    asm volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    return dst;
}

extern "C" void* memmove(void* dst, const void* src, size_t n) {
    auto* d = static_cast<uint8_t*>(dst);
    auto* s = static_cast<const uint8_t*>(src);
    if (d == s || n == 0) return dst;
    if (d < s || d >= s + n) return memcpy(dst, src, n);
    while (n--) d[n] = s[n];
    return dst;
}

extern "C" void* memset(void* dst, int c, size_t n) {
    auto* d = static_cast<uint8_t*>(dst);
    asm volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return dst;
}

extern "C" int memcmp(const void* a, const void* b, size_t n) {
    auto* x = static_cast<const uint8_t*>(a);
    auto* y = static_cast<const uint8_t*>(b);
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}

extern "C" size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

extern "C" int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

extern "C" int strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i]) return (uint8_t)a[i] - (uint8_t)b[i];
    }
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

namespace {
struct Out {
    char* buf;
    size_t size;
    size_t pos;
    void put(char c) {
        if (pos + 1 < size) buf[pos] = c;
        pos++;
    }
};

void put_num(Out& o, uint32_t v, unsigned base, int width, bool zero, bool neg, bool upper) {
    char tmp[16];
    int n = 0;
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do { tmp[n++] = digits[v % base]; v /= base; } while (v);
    int len = n + (neg ? 1 : 0);
    if (neg && zero) o.put('-');
    for (; len < width; len++) o.put(zero ? '0' : ' ');
    if (neg && !zero) o.put('-');
    while (n) o.put(tmp[--n]);
}
}

int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap) {
    Out o{buf, size, 0};
    for (; *fmt; fmt++) {
        if (*fmt != '%') { o.put(*fmt); continue; }
        fmt++;
        bool zero = false;
        int width = 0;
        if (*fmt == '0') { zero = true; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        if (*fmt == 'l') fmt++;
        switch (*fmt) {
        case 'd': case 'i': {
            int32_t v = va_arg(ap, int32_t);
            put_num(o, v < 0 ? -(uint32_t)v : v, 10, width, zero, v < 0, false);
            break;
        }
        case 'u': put_num(o, va_arg(ap, uint32_t), 10, width, zero, false, false); break;
        case 'x': put_num(o, va_arg(ap, uint32_t), 16, width, zero, false, false); break;
        case 'X': put_num(o, va_arg(ap, uint32_t), 16, width, zero, false, true); break;
        case 'p': o.put('0'); o.put('x'); put_num(o, va_arg(ap, uint32_t), 16, 8, true, false, false); break;
        case 'c': o.put((char)va_arg(ap, int)); break;
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int len = (int)strlen(s);
            for (; len < width; len++) o.put(' ');
            while (*s) o.put(*s++);
            break;
        }
        case '%': o.put('%'); break;
        case 0: fmt--; break;
        default: o.put('%'); o.put(*fmt); break;
        }
    }
    if (size) buf[o.pos < size ? o.pos : size - 1] = 0;
    return (int)o.pos;
}

int snprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}
