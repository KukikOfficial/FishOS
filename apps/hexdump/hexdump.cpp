// hexdump: prints files (or standard input) as hex and ASCII.
#include <fihcore/syscall.h>
#include <stdio.h>

static int dump(int fd) {
    unsigned char buf[16];
    uint32_t off = 0;
    for (;;) {
        int n = 0;
        while (n < 16) { // fill a whole line unless the input ends
            int r = fih_read(fd, buf + n, (uint32_t)(16 - n));
            if (r < 0) return r;
            if (r == 0) break;
            n += r;
        }
        if (n == 0) return 0;
        char line[96];
        int o = snprintf(line, sizeof line, "%08x  ", off);
        for (int i = 0; i < 16; i++)
            o += i < n ? snprintf(line + o, sizeof line - (size_t)o, "%02x ", buf[i]) : snprintf(line + o, sizeof line - (size_t)o, "   ");
        o += snprintf(line + o, sizeof line - (size_t)o, " |");
        for (int i = 0; i < n; i++) line[o++] = buf[i] >= 32 && buf[i] < 127 ? (char)buf[i] : '.';
        line[o++] = '|';
        line[o++] = '\n';
        fih_write(1, line, (uint32_t)o);
        off += (uint32_t)n;
        if (n < 16) return 0;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) return dump(0) < 0;
    int status = 0;
    for (int i = 1; i < argc; i++) {
        int fd = fih_open(argv[i], FIH_O_READ);
        if (fd < 0) {
            dprintf(2, "hexdump: %s: cannot open (%d)\n", argv[i], fd);
            status = 1;
            continue;
        }
        if (dump(fd) < 0) status = 1;
        fih_close(fd);
    }
    return status;
}
