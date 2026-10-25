// Host unit tests for FihFS against an in-memory block device.
#include "fs/fihfs.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
struct Mem {
    uint8_t* data;
    uint32_t blocks;
    int fail_writes_after; // -1 = never
};
int mread(void* c, uint32_t b, void* buf) {
    Mem* m = (Mem*)c;
    if (b >= m->blocks) return -FIH_EIO;
    memcpy(buf, m->data + (size_t)b * FIHFS_BLOCK, FIHFS_BLOCK);
    return 0;
}
int mwrite(void* c, uint32_t b, const void* buf) {
    Mem* m = (Mem*)c;
    if (b >= m->blocks) return -FIH_EIO;
    if (m->fail_writes_after == 0) return -FIH_EIO;
    if (m->fail_writes_after > 0) m->fail_writes_after--;
    memcpy(m->data + (size_t)b * FIHFS_BLOCK, buf, FIHFS_BLOCK);
    return 0;
}
uint32_t mnow(void*) { return 1234; }

int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define EQ(a, b) do { long _a = (long)(a), _b = (long)(b); checks++; if (_a != _b) { failures++; printf("FAIL %s:%d: %s = %ld, expected %ld\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

void report(void*, const char* m) { printf("  fsck: %s\n", m); }

Mem mem;
FihfsDev dev;
Fihfs* fs;

void fresh(uint32_t mib) {
    free(mem.data);
    mem.blocks = mib * 256;
    mem.data = (uint8_t*)calloc(mem.blocks, FIHFS_BLOCK);
    mem.fail_writes_after = -1;
    dev = FihfsDev{&mem, mem.blocks, mread, mwrite, mnow};
    EQ(fihfs_mkfs(&dev, "test"), 0);
    EQ(fihfs_mount(fs, &dev), 0);
}

void remount() { EQ(fihfs_mount(fs, &dev), 0); }
void clean() { EQ(fihfs_check(fs, report, nullptr), 0); }

uint32_t make_file(const char* path, const char* text) {
    uint32_t ino = 0;
    EQ(fihfs_create(fs, path, FIH_FT_FILE, &ino), 0);
    EQ(fihfs_write(fs, ino, 0, text, (uint32_t)strlen(text)), (int)strlen(text));
    return ino;
}

bool content_is(const char* path, const char* text) {
    uint32_t ino;
    if (fihfs_lookup(fs, path, &ino)) return false;
    char buf[256] = {};
    int n = fihfs_read(fs, ino, 0, buf, sizeof buf - 1);
    return n == (int)strlen(text) && memcmp(buf, text, n) == 0;
}

void test_basic() {
    fresh(4);
    EQ(fihfs_create(fs, "/docs", FIH_FT_DIR, nullptr), 0);
    fih_statfs_t before; // root now owns its first directory block
    fihfs_statfs(fs, &before);
    EQ(fihfs_create(fs, "/docs", FIH_FT_DIR, nullptr), -FIH_EEXIST);
    make_file("/docs/note.txt", "hello\nworld\n");
    remount();
    CHECK(content_is("/docs/note.txt", "hello\nworld\n"));
    uint32_t ino;
    EQ(fihfs_lookup(fs, "/missing", &ino), -FIH_ENOENT);
    EQ(fihfs_lookup(fs, "/docs/note.txt/x", &ino), -FIH_ENOTDIR);
    EQ(fihfs_create(fs, "/nope/x", FIH_FT_FILE, nullptr), -FIH_ENOENT);
    EQ(fihfs_create(fs, "/docs/..", FIH_FT_FILE, nullptr), -FIH_EINVAL);
    char longname[80];
    memset(longname, 'a', sizeof longname);
    longname[0] = '/';
    longname[79] = 0;
    EQ(fihfs_create(fs, longname, FIH_FT_FILE, nullptr), -FIH_ENAMETOOLONG);
    fih_dirent_t d;
    EQ(fihfs_lookup(fs, "/docs", &ino), 0);
    EQ(fihfs_readdir(fs, ino, 0, &d), 1);
    CHECK(!strcmp(d.name, "note.txt") && d.type == FIH_FT_FILE && d.size == 12);
    EQ(fihfs_readdir(fs, ino, 1, &d), 0);
    EQ(fihfs_unlink(fs, "/docs"), -FIH_ENOTEMPTY);
    EQ(fihfs_unlink(fs, "/"), -FIH_EBUSY);
    EQ(fihfs_unlink(fs, "/docs/note.txt"), 0);
    EQ(fihfs_unlink(fs, "/docs"), 0);
    fih_statfs_t after;
    fihfs_statfs(fs, &after);
    EQ(after.free_blocks, before.free_blocks);
    EQ(after.free_inodes, before.free_inodes + 1);
    clean();
}

void test_large_file() {
    fresh(16);
    uint32_t ino;
    EQ(fihfs_create(fs, "/big", FIH_FT_FILE, &ino), 0);
    const uint32_t size = 3 * 1024 * 1024 + 123; // needs the indirect block
    uint8_t* buf = (uint8_t*)malloc(size);
    for (uint32_t i = 0; i < size; i++) buf[i] = (uint8_t)(i * 7 + (i >> 12));
    EQ(fihfs_write(fs, ino, 0, buf, size), (int)size);
    remount();
    uint8_t* back = (uint8_t*)malloc(size);
    EQ(fihfs_read(fs, ino, 0, back, size), (int)size);
    CHECK(memcmp(buf, back, size) == 0);
    // Unaligned overwrite across a block boundary.
    EQ(fihfs_write(fs, ino, 4090, "ABCDEFGHIJ", 10), 10);
    EQ(fihfs_read(fs, ino, 4088, back, 14), 14);
    CHECK(back[0] == buf[4088] && memcmp(back + 2, "ABCDEFGHIJ", 10) == 0 && back[12] == buf[4100]);
    // Truncate into the direct range, then grow: the gap must read as zeros.
    EQ(fihfs_truncate(fs, ino, 5000), 0);
    EQ(fihfs_write(fs, ino, 9000, "Z", 1), 1);
    EQ(fihfs_read(fs, ino, 4999, back, 4002), 4002);
    CHECK(back[0] == buf[4999] || back[0] != 0);
    bool zeros = true;
    for (int i = 1; i < 4001; i++) zeros &= back[i] == 0;
    CHECK(zeros);
    CHECK(back[4001] == 'Z');
    clean();
    // Maximum size is enforced.
    EQ(fihfs_write(fs, ino, FIHFS_MAX_FILE, "x", 1), -FIH_EFBIG);
    EQ(fihfs_truncate(fs, ino, 0), 0);
    clean();
    free(buf);
    free(back);
}

void test_rename() {
    fresh(4);
    fihfs_create(fs, "/a", FIH_FT_DIR, nullptr);
    fihfs_create(fs, "/a/b", FIH_FT_DIR, nullptr);
    fihfs_create(fs, "/c", FIH_FT_DIR, nullptr);
    make_file("/a/f.txt", "one");
    make_file("/c/g.txt", "two");
    EQ(fihfs_rename(fs, "/a/f.txt", "/a/h.txt", 0), 0);
    CHECK(content_is("/a/h.txt", "one"));
    EQ(fihfs_rename(fs, "/a/h.txt", "/c/h.txt", 0), 0);
    CHECK(content_is("/c/h.txt", "one"));
    EQ(fihfs_rename(fs, "/c/h.txt", "/c/g.txt", 0), -FIH_EEXIST);
    EQ(fihfs_rename(fs, "/c/h.txt", "/c/g.txt", FIH_RENAME_REPLACE), 0);
    CHECK(content_is("/c/g.txt", "one"));
    uint32_t ino;
    EQ(fihfs_lookup(fs, "/c/h.txt", &ino), -FIH_ENOENT);
    EQ(fihfs_rename(fs, "/a", "/a/b/a", 0), -FIH_EINVAL); // into itself
    EQ(fihfs_rename(fs, "/a", "/c/a", 0), 0);
    EQ(fihfs_lookup(fs, "/c/a/b", &ino), 0);
    EQ(fihfs_rename(fs, "/c/a", "/c/g.txt", FIH_RENAME_REPLACE), -FIH_EEXIST); // dir over file
    remount();
    clean();
}

void test_many_entries_and_space() {
    fresh(1);
    fih_statfs_t sf;
    fihfs_statfs(fs, &sf);
    char path[64];
    int created = 0;
    for (int i = 0; i < 2000; i++) {
        snprintf(path, sizeof path, "/f%04d", i);
        int e = fihfs_create(fs, path, FIH_FT_FILE, nullptr);
        if (e == -FIH_ENOSPC) break;
        EQ(e, 0);
        created++;
    }
    CHECK(created > 32);
    remount();
    clean();
    for (int i = 0; i < created; i += 2) {
        snprintf(path, sizeof path, "/f%04d", i);
        EQ(fihfs_unlink(fs, path), 0);
    }
    // Fill the data area: writes stop with a short count / ENOSPC, never corrupt.
    uint32_t ino;
    EQ(fihfs_create(fs, "/fill", FIH_FT_FILE, &ino), 0);
    static uint8_t chunk[FIHFS_BLOCK];
    uint32_t off = 0;
    for (;;) {
        int n = fihfs_write(fs, ino, off, chunk, sizeof chunk);
        if (n <= 0) { EQ(n, -FIH_ENOSPC); break; }
        off += n;
    }
    remount();
    clean();
}

void test_corruption() {
    fresh(2);
    make_file("/x", "data");
    // Bad checksum: refuse to mount.
    mem.data[40] ^= 0xFF;
    EQ(fihfs_mount(fs, &dev), -FIH_ECORRUPT);
    mem.data[40] ^= 0xFF;
    remount();
    // Not a FihFS volume.
    memset(mem.data, 0, 8);
    EQ(fihfs_mount(fs, &dev), -FIH_ENODEV);
    fresh(2);
    uint32_t ino = make_file("/x", "data");
    // Out-of-range block pointer in an inode is reported, not followed.
    FihfsSuper sb = fs->sb;
    FihfsInode* in = (FihfsInode*)(mem.data + (size_t)sb.itable_start * FIHFS_BLOCK) + (ino - 1);
    in->direct[0] = sb.total_blocks + 5;
    char buf[8];
    EQ(fihfs_read(fs, ino, 0, buf, 4), -FIH_ECORRUPT);
    CHECK(fihfs_check(fs, nullptr, nullptr) > 0);
}

void test_io_error() {
    fresh(2);
    uint32_t ino;
    EQ(fihfs_create(fs, "/f", FIH_FT_FILE, &ino), 0);
    mem.fail_writes_after = 0;
    char buf[100] = {};
    EQ(fihfs_write(fs, ino, 0, buf, sizeof buf), -FIH_EIO);
    EQ(fihfs_create(fs, "/g", FIH_FT_FILE, nullptr), -FIH_EIO);
    mem.fail_writes_after = -1;
    remount();
    EQ(fihfs_lookup(fs, "/f", &ino), 0);
}
}

int main() {
    fs = (Fihfs*)calloc(1, sizeof(Fihfs));
    test_basic();
    test_large_file();
    test_rename();
    test_many_entries_and_space();
    test_corruption();
    test_io_error();
    printf("fihfs: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
