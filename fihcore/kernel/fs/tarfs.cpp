// Read-only filesystem over a ustar archive in memory (the boot module).
// Inode numbers are 1 for the root and (entry index + 2) for archive members.
#include "vfs.hpp"
#include "lib/lib.hpp"
#include "mm/heap.hpp"

namespace {
struct Entry {
    char path[FIH_PATH_MAX]; // normalized, absolute, no trailing slash
    uint32_t type;
    const uint8_t* data;
    uint32_t size;
};
struct Tar {
    Entry* entries;
    uint32_t count;
};

uint32_t octal(const char* s, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n && s[i] >= '0' && s[i] <= '7'; i++) v = v * 8 + (uint32_t)(s[i] - '0');
    return v;
}

Tar* T(Fs* fs) { return static_cast<Tar*>(fs->priv); }

// Parent directory of `path` (length of the prefix before the last slash).
size_t parent_len(const char* path) {
    size_t n = strlen(path);
    while (n > 0 && path[n - 1] != '/') n--;
    return n > 1 ? n - 1 : 1;
}

int t_lookup(Fs* fs, const char* path, uint32_t* ino) {
    if (strcmp(path, "/") == 0) { *ino = 1; return 0; }
    Tar* t = T(fs);
    for (uint32_t i = 0; i < t->count; i++)
        if (strcmp(t->entries[i].path, path) == 0) { *ino = i + 2; return 0; }
    return -FIH_ENOENT;
}

int t_stat(Fs* fs, uint32_t ino, fih_stat_t* st) {
    memset(st, 0, sizeof(*st));
    st->inode = ino;
    st->flags = FIH_ST_READONLY;
    if (ino == 1) { st->type = FIH_FT_DIR; return 0; }
    Tar* t = T(fs);
    if (ino - 2 >= t->count) return -FIH_ENOENT;
    st->type = t->entries[ino - 2].type;
    st->size = t->entries[ino - 2].size;
    return 0;
}

int t_read(Fs* fs, uint32_t ino, uint32_t off, void* buf, uint32_t n) {
    Tar* t = T(fs);
    if (ino < 2 || ino - 2 >= t->count) return -FIH_EISDIR;
    Entry& e = t->entries[ino - 2];
    if (e.type != FIH_FT_FILE) return -FIH_EISDIR;
    if (off >= e.size) return 0;
    n = kmin(n, e.size - off);
    memcpy(buf, e.data + off, n);
    return (int)n;
}

int t_readdir(Fs* fs, uint32_t ino, uint32_t index, fih_dirent_t* out) {
    Tar* t = T(fs);
    const char* dir = ino == 1 ? "/" : nullptr;
    if (!dir) {
        if (ino - 2 >= t->count || t->entries[ino - 2].type != FIH_FT_DIR) return -FIH_ENOTDIR;
        dir = t->entries[ino - 2].path;
    }
    size_t dlen = strlen(dir);
    uint32_t n = 0;
    for (uint32_t i = 0; i < t->count; i++) {
        Entry& e = t->entries[i];
        size_t pl = parent_len(e.path);
        if (pl != dlen || strncmp(e.path, dir, dlen) != 0) continue;
        if (n++ != index) continue;
        memset(out, 0, sizeof(*out));
        strlcpy(out->name, e.path + (dlen == 1 ? 1 : dlen + 1), sizeof(out->name));
        out->type = e.type;
        out->size = e.size;
        return 1;
    }
    return 0;
}

int t_statfs(Fs* fs, fih_statfs_t* out) {
    memset(out, 0, sizeof(*out));
    out->block_size = 512;
    out->total_inodes = T(fs)->count + 1;
    out->flags = FIH_ST_READONLY;
    strlcpy(out->fstype, "tarfs", sizeof(out->fstype));
    return 0;
}

const FsOps ops = {
    t_lookup, t_stat, t_read,
    [](Fs*, uint32_t, uint32_t, const void*, uint32_t) { return -FIH_EROFS; },
    [](Fs*, uint32_t, uint32_t) { return -FIH_EROFS; },
    [](Fs*, const char*, uint32_t, uint32_t*) { return -FIH_EROFS; },
    [](Fs*, const char*) { return -FIH_EROFS; },
    [](Fs*, const char*, const char*, int) { return -FIH_EROFS; },
    t_readdir, t_statfs};
}

Fs* tarfs_create(const uint8_t* data, uint32_t size) {
    // First pass counts entries so the table is allocated once.
    uint32_t count = 0;
    for (uint32_t off = 0; off + 512 <= size;) {
        const uint8_t* h = data + off;
        if (!h[0]) break;
        uint32_t fsize = octal((const char*)h + 124, 12);
        count++;
        off += 512 + align_up(fsize, 512);
    }
    Tar* t = (Tar*)kzalloc(sizeof(Tar));
    Fs* fs = (Fs*)kzalloc(sizeof(Fs));
    if (!t || !fs) return nullptr;
    t->entries = (Entry*)kzalloc(sizeof(Entry) * (count ? count : 1));
    if (!t->entries) return nullptr;
    for (uint32_t off = 0; off + 512 <= size && t->count < count;) {
        const uint8_t* h = data + off;
        if (!h[0]) break;
        uint32_t fsize = octal((const char*)h + 124, 12);
        char type = (char)h[156];
        if (off + 512 + fsize > size) {
            klog("tarfs: truncated archive entry, ignoring the rest\n");
            break;
        }
        char raw[FIH_PATH_MAX];
        size_t n = 0;
        if (memcmp(h + 257, "ustar", 5) == 0 && h[345]) { // ustar prefix field
            for (int i = 0; i < 155 && h[345 + i] && n < sizeof(raw) - 2; i++) raw[n++] = (char)h[345 + i];
            raw[n++] = '/';
        }
        for (int i = 0; i < 100 && h[i] && n < sizeof(raw) - 1; i++) raw[n++] = (char)h[i];
        raw[n] = 0;
        Entry& e = t->entries[t->count];
        if ((type == '0' || type == 0 || type == '5') && vfs_normalize("/", raw, e.path) == 0 &&
            strcmp(e.path, "/") != 0) {
            e.type = type == '5' ? FIH_FT_DIR : FIH_FT_FILE;
            e.data = h + 512;
            e.size = e.type == FIH_FT_FILE ? fsize : 0;
            t->count++;
        }
        off += 512 + align_up(fsize, 512);
    }
    fs->ops = &ops;
    fs->priv = t;
    fs->readonly = true;
    strlcpy(fs->device, "initrd", sizeof(fs->device));
    klog("tarfs: %u entries\n", t->count);
    return fs;
}
