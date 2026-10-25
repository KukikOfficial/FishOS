#include "fishos/fs.h"
#include <fihcore/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace fs {

static int normalize(const char* in, char* out, size_t size) {
    size_t o = 0;
    if (size < 2) return -FIH_ENAMETOOLONG;
    out[o++] = '/';
    const char* p = in;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char* end = p;
        while (*end && *end != '/') end++;
        size_t len = (size_t)(end - p);
        if (len == 1 && p[0] == '.') {
        } else if (len == 2 && p[0] == '.' && p[1] == '.') {
            if (o > 1) {
                o--;
                while (o > 1 && out[o - 1] != '/') o--;
            }
        } else {
            if (o + len + 1 >= size) return -FIH_ENAMETOOLONG;
            memcpy(out + o, p, len);
            o += len;
            out[o++] = '/';
        }
        p = end;
    }
    if (o > 1) o--;
    out[o] = 0;
    return 0;
}

int absolute(const char* path, char* out, size_t size) {
    char tmp[FIH_PATH_MAX * 2];
    if (path[0] == '/') {
        strlcpy(tmp, path, sizeof tmp);
    } else {
        if (fih_getcwd(tmp, FIH_PATH_MAX) < 0) strlcpy(tmp, "/", sizeof tmp);
        strlcat(tmp, "/", sizeof tmp);
        strlcat(tmp, path, sizeof tmp);
    }
    return normalize(tmp, out, size);
}

int join(const char* dir, const char* name, char* out, size_t size) {
    char tmp[FIH_PATH_MAX * 2];
    snprintf(tmp, sizeof tmp, "%s/%s", dir, name);
    return normalize(tmp, out, size);
}

const char* basename(const char* path) {
    const char* s = strrchr(path, '/');
    return s && s[1] ? s + 1 : path;
}

void dirname(const char* path, char* out, size_t size) {
    strlcpy(out, path, size);
    char* s = strrchr(out, '/');
    if (!s) strlcpy(out, ".", size);
    else if (s == out) out[1] = 0;
    else *s = 0;
}

bool exists(const char* path) {
    fih_stat_t st;
    return fih_stat(path, &st) == 0;
}

bool is_dir(const char* path) {
    fih_stat_t st;
    return fih_stat(path, &st) == 0 && st.type == FIH_FT_DIR;
}

int read_all(const char* path, char** data, uint32_t* len, uint32_t max) {
    *data = nullptr;
    *len = 0;
    int fd = fih_open(path, FIH_O_READ);
    if (fd < 0) return fd;
    fih_stat_t st;
    int e = fih_fstat(fd, &st);
    if (e == 0 && st.type == FIH_FT_DIR) e = -FIH_EISDIR;
    if (e == 0 && st.size > max) e = -FIH_EFBIG;
    char* buf = e ? nullptr : static_cast<char*>(malloc(st.size + 1));
    if (!e && !buf) e = -FIH_ENOMEM;
    uint32_t got = 0;
    while (!e && got < st.size) {
        int n = fih_read(fd, buf + got, st.size - got);
        if (n < 0) e = n;
        if (n <= 0) break;
        got += (uint32_t)n;
    }
    fih_close(fd);
    if (e) {
        free(buf);
        return e;
    }
    buf[got] = 0;
    *data = buf;
    *len = got;
    return 0;
}

static int write_fd(int fd, const char* data, uint32_t len) {
    uint32_t done = 0;
    while (done < len) {
        int n = fih_write(fd, data + done, len - done);
        if (n < 0) return n;
        if (n == 0) return -FIH_ENOSPC;
        done += (uint32_t)n;
    }
    return 0;
}

int write_atomic(const char* path, const char* data, uint32_t len) {
    char tmp[FIH_PATH_MAX];
    char dir[FIH_PATH_MAX];
    dirname(path, dir, sizeof dir);
    if (snprintf(tmp, sizeof tmp, "%s/.%s.saving", dir, basename(path)) >= (int)sizeof tmp)
        return -FIH_ENAMETOOLONG;
    int fd = fih_open(tmp, FIH_O_WRITE | FIH_O_CREATE | FIH_O_TRUNC);
    if (fd < 0) return fd;
    int e = write_fd(fd, data, len);
    fih_close(fd);
    if (e == 0) {
        fih_stat_t st;
        e = fih_stat(tmp, &st);
        if (e == 0 && st.size != len) e = -FIH_EIO;
    }
    if (e == 0) e = fih_rename(tmp, path, FIH_RENAME_REPLACE);
    if (e) fih_unlink(tmp);
    return e;
}

static int copy_file(const char* from, const char* to) {
    int in = fih_open(from, FIH_O_READ);
    if (in < 0) return in;
    int out = fih_open(to, FIH_O_WRITE | FIH_O_CREATE | FIH_O_EXCL);
    if (out < 0) {
        fih_close(in);
        return out;
    }
    static char buf[16384];
    int e = 0;
    for (;;) {
        int n = fih_read(in, buf, sizeof buf);
        if (n < 0) e = n;
        if (n <= 0) break;
        if ((e = write_fd(out, buf, (uint32_t)n))) break;
    }
    fih_close(in);
    fih_close(out);
    if (e) fih_unlink(to);
    return e;
}

static int copy_rec(const char* from, const char* to, int depth) {
    if (depth > 32) return -FIH_ENAMETOOLONG;
    fih_stat_t st;
    int e = fih_stat(from, &st);
    if (e) return e;
    if (st.type != FIH_FT_DIR) return copy_file(from, to);
    if ((e = fih_mkdir(to))) return e;
    int dir = fih_open(from, FIH_O_READ);
    if (dir < 0) return dir;
    fih_dirent_t d;
    char a[FIH_PATH_MAX], b[FIH_PATH_MAX];
    for (uint32_t i = 0; fih_readdir(dir, i, &d) == 1; i++) {
        if ((e = join(from, d.name, a, sizeof a)) || (e = join(to, d.name, b, sizeof b))) break;
        if ((e = copy_rec(a, b, depth + 1))) break;
    }
    fih_close(dir);
    return e;
}

int copy(const char* from, const char* to) {
    char a[FIH_PATH_MAX], b[FIH_PATH_MAX];
    int e = absolute(from, a, sizeof a);
    if (!e) e = absolute(to, b, sizeof b);
    if (e) return e;
    size_t n = strlen(a);
    if (strncmp(a, b, n) == 0 && (b[n] == '/' || b[n] == 0)) return -FIH_EINVAL; // into itself
    if (exists(b)) return -FIH_EEXIST;
    return copy_rec(a, b, 0);
}

int remove_tree(const char* path) {
    fih_stat_t st;
    int e = fih_stat(path, &st);
    if (e) return e;
    if (st.type == FIH_FT_DIR) {
        int dir = fih_open(path, FIH_O_READ);
        if (dir < 0) return dir;
        fih_dirent_t d;
        char child[FIH_PATH_MAX];
        // Always read entry 0: deleting shifts the remaining entries down.
        while (fih_readdir(dir, 0, &d) == 1) {
            if ((e = join(path, d.name, child, sizeof child)) || (e = remove_tree(child))) break;
        }
        fih_close(dir);
        if (e) return e;
    }
    return fih_unlink(path);
}

} // namespace fs
