#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <fihcore/syscall.h>

typedef struct { char* buf; size_t size, pos; } Out;
static void put(Out* o, char c) {
    if (o->pos + 1 < o->size) o->buf[o->pos] = c;
    o->pos++;
}
static void pad(Out* o, int n, char c) { while (n-- > 0) put(o, c); }

int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap) {
    Out o = {buf, size, 0};
    for (; *fmt; fmt++) {
        if (*fmt != '%') { put(&o, *fmt); continue; }
        fmt++;
        int left = 0, zero = 0, width = 0, prec = -1;
        for (;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') zero = 1;
            else break;
        }
        if (*fmt == '*') { width = va_arg(ap, int); fmt++; }
        else while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            else while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0');
        }
        while (*fmt == 'l' || *fmt == 'z') fmt++;
        char tmp[16];
        const char* s = tmp;
        int len = 0, neg = 0;
        switch (*fmt) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'p': {
            uint32_t v;
            unsigned base = (*fmt == 'x' || *fmt == 'X' || *fmt == 'p') ? 16 : 10;
            if (*fmt == 'd' || *fmt == 'i') {
                int32_t sv = va_arg(ap, int32_t);
                neg = sv < 0;
                v = neg ? -(uint32_t)sv : (uint32_t)sv;
            } else {
                v = va_arg(ap, uint32_t);
            }
            const char* dig = *fmt == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char r[16];
            int n = 0;
            do { r[n++] = dig[v % base]; v /= base; } while (v);
            if (*fmt == 'p') { while (n < 8) r[n++] = '0'; r[n++] = 'x'; r[n++] = '0'; }
            if (neg) r[n++] = '-';
            for (int i = 0; i < n; i++) tmp[i] = r[n - 1 - i];
            len = n;
            break;
        }
        case 'c': tmp[0] = (char)va_arg(ap, int); len = 1; break;
        case 's':
            s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            len = (int)strlen(s);
            if (prec >= 0 && len > prec) len = prec;
            break;
        case '%': tmp[0] = '%'; len = 1; break;
        case 0: fmt--; continue;
        default: tmp[0] = '%'; tmp[1] = *fmt; len = 2; break;
        }
        if (!left) {
            if (zero && neg && s == tmp) { put(&o, '-'); s++; len--; width--; }
            pad(&o, width - len, zero && s == tmp ? '0' : ' ');
        }
        for (int i = 0; i < len; i++) put(&o, s[i]);
        if (left) pad(&o, width - len, ' ');
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

static int vdprintf(int fd, const char* fmt, va_list ap) {
    char buf[1024];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
    return fih_write(fd, buf, (uint32_t)n);
}

int dprintf(int fd, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vdprintf(fd, fmt, ap);
    va_end(ap);
    return n;
}

int printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vdprintf(1, fmt, ap);
    va_end(ap);
    return n;
}

int puts(const char* s) {
    fih_write(1, s, strlen(s));
    return fih_write(1, "\n", 1);
}
