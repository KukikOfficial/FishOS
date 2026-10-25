// Binds FihFS to an ATA disk through a small write-through block cache.
#include "vfs.hpp"
#include "fihfs.hpp"
#include "dev/dev.hpp"
#include "lib/lib.hpp"
#include "mm/heap.hpp"

uint32_t clock_epoch();

namespace {
constexpr uint32_t CACHE_BLOCKS = 64;
constexpr uint32_t SECTORS = FIHFS_BLOCK / 512;

struct CacheEntry {
    uint32_t block;
    uint32_t stamp; // 0 = empty
    uint8_t data[FIHFS_BLOCK];
};

struct Disk {
    int ata;
    uint32_t clock;
    CacheEntry* cache;
    Fihfs fs;
};

CacheEntry* lookup(Disk* d, uint32_t block) {
    for (uint32_t i = 0; i < CACHE_BLOCKS; i++)
        if (d->cache[i].stamp && d->cache[i].block == block) return &d->cache[i];
    return nullptr;
}

CacheEntry* victim(Disk* d) {
    CacheEntry* v = &d->cache[0];
    for (uint32_t i = 0; i < CACHE_BLOCKS; i++) {
        if (!d->cache[i].stamp) return &d->cache[i];
        if (d->cache[i].stamp < v->stamp) v = &d->cache[i];
    }
    return v;
}

int dev_read(void* ctx, uint32_t block, void* buf) {
    Disk* d = static_cast<Disk*>(ctx);
    CacheEntry* c = lookup(d, block);
    if (!c) {
        c = victim(d);
        c->stamp = 0;
        int e = ata_read(d->ata, block * SECTORS, SECTORS, c->data);
        if (e) return e;
        c->block = block;
    }
    c->stamp = ++d->clock;
    memcpy(buf, c->data, FIHFS_BLOCK);
    return 0;
}

int dev_write(void* ctx, uint32_t block, const void* buf) {
    Disk* d = static_cast<Disk*>(ctx);
    CacheEntry* c = lookup(d, block);
    int e = ata_write(d->ata, block * SECTORS, SECTORS, buf);
    if (e) {
        if (c) c->stamp = 0; // device state unknown: drop the cached copy
        return e;
    }
    if (!c) c = victim(d);
    memcpy(c->data, buf, FIHFS_BLOCK);
    c->block = block;
    c->stamp = ++d->clock;
    return 0;
}

uint32_t dev_now(void*) { return clock_epoch(); }

Fihfs* F(Fs* fs) { return &static_cast<Disk*>(fs->priv)->fs; }
int f_lookup(Fs* fs, const char* p, uint32_t* ino) { return fihfs_lookup(F(fs), p, ino); }
int f_stat(Fs* fs, uint32_t ino, fih_stat_t* st) { return fihfs_stat(F(fs), ino, st); }
int f_read(Fs* fs, uint32_t ino, uint32_t off, void* b, uint32_t n) { return fihfs_read(F(fs), ino, off, b, n); }
int f_write(Fs* fs, uint32_t ino, uint32_t off, const void* b, uint32_t n) { return fihfs_write(F(fs), ino, off, b, n); }
int f_truncate(Fs* fs, uint32_t ino, uint32_t size) { return fihfs_truncate(F(fs), ino, size); }
int f_create(Fs* fs, const char* p, uint32_t type, uint32_t* ino) { return fihfs_create(F(fs), p, type, ino); }
int f_unlink(Fs* fs, const char* p) { return fihfs_unlink(F(fs), p); }
int f_rename(Fs* fs, const char* a, const char* b, int flags) { return fihfs_rename(F(fs), a, b, flags); }
int f_readdir(Fs* fs, uint32_t ino, uint32_t i, fih_dirent_t* out) { return fihfs_readdir(F(fs), ino, i, out); }
int f_statfs(Fs* fs, fih_statfs_t* out) { return fihfs_statfs(F(fs), out); }
const FsOps ops = {f_lookup, f_stat, f_read, f_write, f_truncate, f_create, f_unlink, f_rename, f_readdir, f_statfs};
}

Fs* fihfs_open_device(int ata) {
    if (!g_ata[ata].present) return nullptr;
    Disk* d = (Disk*)kzalloc(sizeof(Disk));
    Fs* fs = (Fs*)kzalloc(sizeof(Fs));
    CacheEntry* cache = (CacheEntry*)kzalloc(sizeof(CacheEntry) * CACHE_BLOCKS);
    if (!d || !fs || !cache) {
        kfree(d); kfree(fs); kfree(cache);
        klog("fihfs: out of memory\n");
        return nullptr;
    }
    d->ata = ata;
    d->cache = cache;
    FihfsDev dev{d, g_ata[ata].sectors / SECTORS, dev_read, dev_write, dev_now};
    int e = fihfs_mount(&d->fs, &dev);
    if (e) {
        if (e != -FIH_ENODEV) klog("ata%d: FihFS volume rejected (error %d), not mounting\n", ata, e);
        kfree(d); kfree(fs); kfree(cache);
        return nullptr;
    }
    fs->ops = &ops;
    fs->priv = d;
    fs->readonly = false;
    snprintf(fs->device, sizeof(fs->device), "ata%d", ata);
    klog("ata%d: FihFS '%s' mounted, %u/%u blocks free\n", ata, d->fs.sb.label, d->fs.free_blocks,
         d->fs.sb.total_blocks);
    return fs;
}
