#include "vfs.hpp"
#include "lib/lib.hpp"
#include "mm/heap.hpp"

namespace {
struct Mount {
    char path[32];
    uint32_t len;
    Fs* fs;
};
constexpr int MAX_MOUNTS = 8;
Mount mounts[MAX_MOUNTS];
int nmounts;

// Root placeholder used when no data disk is mounted at "/".
int empty_lookup(Fs*, const char* p, uint32_t* ino) {
    if (strcmp(p, "/") != 0) return -FIH_ENOENT;
    *ino = 1;
    return 0;
}
int empty_stat(Fs*, uint32_t, fih_stat_t* st) {
    memset(st, 0, sizeof(*st));
    st->type = FIH_FT_DIR;
    st->inode = 1;
    st->flags = FIH_ST_READONLY;
    return 0;
}
int empty_readdir(Fs*, uint32_t, uint32_t, fih_dirent_t*) { return 0; }
int empty_statfs(Fs*, fih_statfs_t* out) {
    memset(out, 0, sizeof(*out));
    strlcpy(out->fstype, "none", sizeof(out->fstype));
    out->flags = FIH_ST_READONLY;
    return 0;
}
const FsOps empty_ops = {
    empty_lookup, empty_stat,
    [](Fs*, uint32_t, uint32_t, void*, uint32_t) { return -FIH_EISDIR; },
    [](Fs*, uint32_t, uint32_t, const void*, uint32_t) { return -FIH_ENODEV; },
    [](Fs*, uint32_t, uint32_t) { return -FIH_ENODEV; },
    [](Fs*, const char*, uint32_t, uint32_t*) { return -FIH_ENODEV; },
    [](Fs*, const char*) { return -FIH_ENODEV; },
    [](Fs*, const char*, const char*, int) { return -FIH_ENODEV; },
    empty_readdir, empty_statfs};
Fs empty_root = {&empty_ops, nullptr, true, "none"};

File console_file = {1, FIH_FT_CONSOLE, FIH_O_READ | FIH_O_WRITE, nullptr, 0, 0, nullptr};

// Finds the mount owning `abs` and the path inside it.
Mount* find(const char* abs, const char** rel) {
    Mount* best = nullptr;
    for (int i = 0; i < nmounts; i++) {
        Mount& m = mounts[i];
        bool match = m.len == 1 || (strncmp(abs, m.path, m.len) == 0 && (abs[m.len] == 0 || abs[m.len] == '/'));
        if (match && (!best || m.len > best->len)) best = &m;
    }
    if (best) *rel = best->len == 1 ? abs : (abs[best->len] ? abs + best->len : "/");
    return best;
}

bool is_mount_point(const char* abs) {
    for (int i = 0; i < nmounts; i++)
        if (strcmp(mounts[i].path, abs) == 0) return true;
    return false;
}
}

void vfs_init() {
    nmounts = 0;
    vfs_mount("/", &empty_root);
}

int vfs_mount(const char* path, Fs* fs) {
    for (int i = 0; i < nmounts; i++) {
        if (strcmp(mounts[i].path, path) == 0) {
            mounts[i].fs = fs;
            return 0;
        }
    }
    if (nmounts == MAX_MOUNTS || strlen(path) >= sizeof(mounts[0].path)) return -FIH_ENOMEM;
    Mount& m = mounts[nmounts++];
    strlcpy(m.path, path, sizeof(m.path));
    m.len = (uint32_t)strlen(path);
    m.fs = fs;
    return 0;
}

int vfs_normalize(const char* cwd, const char* path, char out[FIH_PATH_MAX]) {
    char tmp[FIH_PATH_MAX * 2];
    size_t plen = strlen(path);
    if (plen == 0) return -FIH_ENOENT;
    if (plen >= FIH_PATH_MAX) return -FIH_ENAMETOOLONG;
    if (path[0] == '/') {
        strlcpy(tmp, path, sizeof(tmp));
    } else {
        size_t clen = strlcpy(tmp, cwd, sizeof(tmp));
        tmp[clen] = '/';
        strlcpy(tmp + clen + 1, path, sizeof(tmp) - clen - 1);
    }
    // Rebuild component by component, resolving "." and "..".
    size_t o = 0;
    out[o++] = '/';
    const char* p = tmp;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char* end = p;
        while (*end && *end != '/') end++;
        size_t len = (size_t)(end - p);
        if (len == 1 && p[0] == '.') {
        } else if (len == 2 && p[0] == '.' && p[1] == '.') {
            if (o > 1) {
                o--; // drop trailing slash
                while (o > 1 && out[o - 1] != '/') o--;
            }
        } else {
            if (len > FIH_NAME_MAX) return -FIH_ENAMETOOLONG;
            if (o + len + 1 >= FIH_PATH_MAX) return -FIH_ENAMETOOLONG;
            memcpy(out + o, p, len);
            o += len;
            out[o++] = '/';
        }
        p = end;
    }
    if (o > 1) o--; // no trailing slash except for "/"
    out[o] = 0;
    return 0;
}

int vfs_open(const char* abs, uint32_t flags, File** out) {
    const char* rel;
    Mount* m = find(abs, &rel);
    if (!m) return -FIH_ENOENT;
    Fs* fs = m->fs;
    bool writing = flags & (FIH_O_WRITE | FIH_O_TRUNC | FIH_O_APPEND);
    uint32_t ino;
    int e = fs->ops->lookup(fs, rel, &ino);
    if (e == -FIH_ENOENT && (flags & FIH_O_CREATE)) {
        if (fs->readonly) return -FIH_EROFS;
        e = fs->ops->create(fs, rel, FIH_FT_FILE, &ino);
    } else if (e == 0 && (flags & FIH_O_CREATE) && (flags & FIH_O_EXCL)) {
        return -FIH_EEXIST;
    }
    if (e) return e;
    fih_stat_t st;
    if ((e = fs->ops->stat(fs, ino, &st))) return e;
    if (st.type == FIH_FT_DIR && writing) return -FIH_EISDIR;
    if (writing && fs->readonly) return -FIH_EROFS;
    if ((flags & FIH_O_TRUNC) && (e = fs->ops->truncate(fs, ino, 0))) return e;
    File* f = (File*)kzalloc(sizeof(File));
    if (!f) return -FIH_ENOMEM;
    f->refs = 1;
    f->type = st.type;
    f->flags = flags;
    f->fs = fs;
    f->ino = ino;
    f->pos = 0;
    *out = f;
    return 0;
}

File* vfs_console() { return file_ref(&console_file); }

File* file_ref(File* f) {
    if (f) f->refs++;
    return f;
}

void pipe_close_end(File* f);

void file_close(File* f) {
    if (!f || --f->refs) return;
    if (f->type == FIH_FT_PIPE) pipe_close_end(f);
    if (f != &console_file) kfree(f);
}

int pipe_read(File* f, void* buf, uint32_t n);
int pipe_write(File* f, const void* buf, uint32_t n);

int file_read(File* f, void* buf, uint32_t n) {
    if (!(f->flags & FIH_O_READ)) return -FIH_EBADF;
    switch (f->type) {
    case FIH_FT_CONSOLE: return 0;
    case FIH_FT_PIPE: return pipe_read(f, buf, n);
    case FIH_FT_DIR: return -FIH_EISDIR;
    }
    int r = f->fs->ops->read(f->fs, f->ino, f->pos, buf, n);
    if (r > 0) f->pos += (uint32_t)r;
    return r;
}

int file_write(File* f, const void* buf, uint32_t n) {
    if (!(f->flags & (FIH_O_WRITE | FIH_O_APPEND))) return -FIH_EBADF;
    switch (f->type) {
    case FIH_FT_CONSOLE: klog_write((const char*)buf, n); return (int)n;
    case FIH_FT_PIPE: return pipe_write(f, buf, n);
    case FIH_FT_DIR: return -FIH_EISDIR;
    }
    if (f->flags & FIH_O_APPEND) {
        fih_stat_t st;
        int e = f->fs->ops->stat(f->fs, f->ino, &st);
        if (e) return e;
        f->pos = st.size;
    }
    int r = f->fs->ops->write(f->fs, f->ino, f->pos, buf, n);
    if (r > 0) f->pos += (uint32_t)r;
    return r;
}

int file_seek(File* f, int32_t off, int whence) {
    if (f->type != FIH_FT_FILE) return -FIH_ESPIPE;
    int64_t base;
    if (whence == FIH_SEEK_SET) base = 0;
    else if (whence == FIH_SEEK_CUR) base = f->pos;
    else if (whence == FIH_SEEK_END) {
        fih_stat_t st;
        int e = f->fs->ops->stat(f->fs, f->ino, &st);
        if (e) return e;
        base = st.size;
    } else return -FIH_EINVAL;
    int64_t np = base + off;
    if (np < 0 || np > 0x7FFFFFFF) return -FIH_EINVAL;
    f->pos = (uint32_t)np;
    return (int)np;
}

int file_stat(File* f, fih_stat_t* st) {
    if (f->type == FIH_FT_PIPE || f->type == FIH_FT_CONSOLE) {
        memset(st, 0, sizeof(*st));
        st->type = f->type;
        return 0;
    }
    int e = f->fs->ops->stat(f->fs, f->ino, st);
    if (!e && f->fs->readonly) st->flags |= FIH_ST_READONLY;
    return e;
}

int file_readdir(File* f, uint32_t index, fih_dirent_t* out) {
    if (f->type != FIH_FT_DIR) return -FIH_ENOTDIR;
    int r = f->fs->ops->readdir(f->fs, f->ino, index, out);
    if (r != 0 || f->ino != 1 || f->fs != mounts[0].fs) return r;
    // Past the root filesystem's own entries: append mount points under "/".
    uint32_t count = 0;
    fih_dirent_t tmp;
    while (f->fs->ops->readdir(f->fs, f->ino, count, &tmp) == 1) count++;
    uint32_t want = index - count;
    for (int i = 1; i < nmounts; i++) {
        const char* name = mounts[i].path + 1;
        bool top_level = true;
        for (const char* c = name; *c; c++) if (*c == '/') top_level = false;
        if (!top_level) continue;
        if (want-- == 0) {
            memset(out, 0, sizeof(*out));
            strlcpy(out->name, name, sizeof(out->name));
            out->type = FIH_FT_DIR;
            return 1;
        }
    }
    return 0;
}

int file_truncate(File* f, uint32_t size) {
    if (f->type != FIH_FT_FILE) return -FIH_EINVAL;
    if (!(f->flags & FIH_O_WRITE)) return -FIH_EBADF;
    return f->fs->ops->truncate(f->fs, f->ino, size);
}

int vfs_stat(const char* abs, fih_stat_t* st) {
    const char* rel;
    Mount* m = find(abs, &rel);
    if (!m) return -FIH_ENOENT;
    uint32_t ino;
    int e = m->fs->ops->lookup(m->fs, rel, &ino);
    if (e) return e;
    e = m->fs->ops->stat(m->fs, ino, st);
    if (!e && m->fs->readonly) st->flags |= FIH_ST_READONLY;
    return e;
}

int vfs_mkdir(const char* abs) {
    const char* rel;
    Mount* m = find(abs, &rel);
    if (!m) return -FIH_ENOENT;
    if (is_mount_point(abs)) return -FIH_EEXIST;
    if (m->fs->readonly) return -FIH_EROFS;
    return m->fs->ops->create(m->fs, rel, FIH_FT_DIR, nullptr);
}

int vfs_unlink(const char* abs) {
    if (is_mount_point(abs)) return -FIH_EBUSY;
    const char* rel;
    Mount* m = find(abs, &rel);
    if (!m) return -FIH_ENOENT;
    if (m->fs->readonly) return -FIH_EROFS;
    return m->fs->ops->unlink(m->fs, rel);
}

int vfs_rename(const char* from, const char* to, int flags) {
    if (is_mount_point(from) || is_mount_point(to)) return -FIH_EBUSY;
    const char *rf, *rt;
    Mount* a = find(from, &rf);
    Mount* b = find(to, &rt);
    if (!a || !b) return -FIH_ENOENT;
    if (a != b) return -FIH_EXDEV;
    if (a->fs->readonly) return -FIH_EROFS;
    return a->fs->ops->rename(a->fs, rf, rt, flags);
}

int vfs_statfs(const char* abs, fih_statfs_t* out) {
    const char* rel;
    Mount* m = find(abs, &rel);
    if (!m) return -FIH_ENOENT;
    int e = m->fs->ops->statfs(m->fs, out);
    if (!e) {
        strlcpy(out->device, m->fs->device, sizeof(out->device));
        if (m->fs->readonly) out->flags |= FIH_ST_READONLY;
    }
    return e;
}
