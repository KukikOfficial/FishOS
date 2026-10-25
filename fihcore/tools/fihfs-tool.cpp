// Host utility for FihFS images: create, inspect, populate and check them.
#include "fs/fihfs.hpp"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

namespace {
int dev_read(void* ctx, uint32_t b, void* buf) {
    FILE* f = (FILE*)ctx;
    if (fseek(f, (long)b * FIHFS_BLOCK, SEEK_SET) || fread(buf, FIHFS_BLOCK, 1, f) != 1) return -FIH_EIO;
    return 0;
}
int dev_write(void* ctx, uint32_t b, const void* buf) {
    FILE* f = (FILE*)ctx;
    if (fseek(f, (long)b * FIHFS_BLOCK, SEEK_SET) || fwrite(buf, FIHFS_BLOCK, 1, f) != 1) return -FIH_EIO;
    return 0;
}
uint32_t dev_now(void*) { return (uint32_t)(time(nullptr) - 946684800); } // 2000-01-01

const char* err(int e) {
    switch (-e) {
    case FIH_ENOENT: return "no such file or directory";
    case FIH_EEXIST: return "already exists";
    case FIH_ENOTDIR: return "not a directory";
    case FIH_EISDIR: return "is a directory";
    case FIH_ENOTEMPTY: return "directory not empty";
    case FIH_ENOSPC: return "no space left";
    case FIH_ECORRUPT: return "filesystem corrupt";
    case FIH_ENODEV: return "not a FihFS image";
    case FIH_EIO: return "I/O error";
    case FIH_EINVAL: return "invalid argument";
    case FIH_EFBIG: return "file too large";
    case FIH_ENAMETOOLONG: return "name too long";
    default: return "error";
    }
}

int fail(const char* what, int e) {
    fprintf(stderr, "fihfs-tool: %s: %s (%d)\n", what, err(e), e);
    return 1;
}

FihfsDev open_dev(const char* path, FILE** out) {
    FILE* f = fopen(path, "r+b");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    *out = f;
    return FihfsDev{f, (uint32_t)(size / FIHFS_BLOCK), dev_read, dev_write, dev_now};
}

void print_report(void*, const char* m) { printf("  %s\n", m); }

int usage() {
    fprintf(stderr,
            "usage: fihfs-tool mkfs [-f] IMAGE SIZE_MIB [LABEL]\n"
            "       fihfs-tool ls IMAGE PATH\n"
            "       fihfs-tool cat IMAGE PATH\n"
            "       fihfs-tool put IMAGE HOSTFILE PATH\n"
            "       fihfs-tool mkdir IMAGE PATH\n"
            "       fihfs-tool rm IMAGE PATH\n"
            "       fihfs-tool fsck IMAGE\n");
    return 2;
}
}

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const char* cmd = argv[1];
    if (!strcmp(cmd, "mkfs")) {
        int i = 2;
        bool force = false;
        if (!strcmp(argv[i], "-f")) { force = true; i++; }
        if (argc < i + 2) return usage();
        const char* img = argv[i];
        long mib = strtol(argv[i + 1], nullptr, 10);
        if (mib < 1 || mib > 1024) { fprintf(stderr, "fihfs-tool: size must be 1..1024 MiB\n"); return 1; }
        struct stat st;
        if (!force && stat(img, &st) == 0 && st.st_size > 0) {
            fprintf(stderr, "fihfs-tool: %s exists; refusing to overwrite (use -f)\n", img);
            return 1;
        }
        FILE* f = fopen(img, "w+b");
        if (!f) { perror(img); return 1; }
        if (fseek(f, mib * 1024 * 1024 - 1, SEEK_SET) || fputc(0, f) == EOF) { perror(img); return 1; }
        FihfsDev dev{f, (uint32_t)(mib * 1024 * 1024 / FIHFS_BLOCK), dev_read, dev_write, dev_now};
        int e = fihfs_mkfs(&dev, argc > i + 2 ? argv[i + 2] : "fihfs");
        fclose(f);
        if (e) return fail("mkfs", e);
        return 0;
    }

    FILE* f;
    FihfsDev dev = open_dev(argv[2], &f);
    Fihfs* fs = (Fihfs*)calloc(1, sizeof(Fihfs));
    int e = fihfs_mount(fs, &dev);
    if (e) return fail("mount", e);

    if (!strcmp(cmd, "fsck")) {
        fih_statfs_t sf;
        fihfs_statfs(fs, &sf);
        printf("%s: %u/%u blocks free, %u/%u inodes free\n", argv[2], sf.free_blocks, sf.total_blocks,
               sf.free_inodes, sf.total_inodes);
        int n = fihfs_check(fs, print_report, nullptr);
        printf(n ? "%d problem(s) found\n" : "clean\n", n);
        return n ? 1 : 0;
    }
    if (argc < 4) return usage();
    const char* path = argv[3];
    uint32_t ino;
    if (!strcmp(cmd, "ls")) {
        if ((e = fihfs_lookup(fs, path, &ino))) return fail(path, e);
        fih_dirent_t d;
        for (uint32_t i = 0; (e = fihfs_readdir(fs, ino, i, &d)) == 1; i++)
            printf("%s %8u %s\n", d.type == FIH_FT_DIR ? "d" : "-", d.size, d.name);
        return e < 0 ? fail(path, e) : 0;
    }
    if (!strcmp(cmd, "cat")) {
        if ((e = fihfs_lookup(fs, path, &ino))) return fail(path, e);
        char buf[4096];
        uint32_t off = 0;
        while ((e = fihfs_read(fs, ino, off, buf, sizeof buf)) > 0) {
            fwrite(buf, 1, (size_t)e, stdout);
            off += (uint32_t)e;
        }
        return e < 0 ? fail(path, e) : 0;
    }
    if (!strcmp(cmd, "mkdir")) {
        e = fihfs_create(fs, path, FIH_FT_DIR, nullptr);
        return e ? fail(path, e) : 0;
    }
    if (!strcmp(cmd, "rm")) {
        e = fihfs_unlink(fs, path);
        return e ? fail(path, e) : 0;
    }
    if (!strcmp(cmd, "put")) {
        if (argc < 5) return usage();
        const char* dst = argv[4];
        FILE* in = fopen(argv[3], "rb");
        if (!in) { perror(argv[3]); return 1; }
        e = fihfs_lookup(fs, dst, &ino);
        if (e == -FIH_ENOENT) e = fihfs_create(fs, dst, FIH_FT_FILE, &ino);
        else if (!e) e = fihfs_truncate(fs, ino, 0);
        if (e) return fail(dst, e);
        char buf[8192];
        size_t n;
        uint32_t off = 0;
        while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
            e = fihfs_write(fs, ino, off, buf, (uint32_t)n);
            if (e < 0 || (size_t)e != n) return fail(dst, e < 0 ? e : -FIH_ENOSPC);
            off += (uint32_t)n;
        }
        fclose(in);
        return 0;
    }
    return usage();
}
