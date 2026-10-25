#include "fihfs.hpp"
#include <stddef.h>
#ifdef FIH_KERNEL
#include "lib/lib.hpp"
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

namespace {
const char MAGIC[8] = {'F', 'I', 'H', 'F', 'S', 0, 'v', '1'};
constexpr uint32_t B = FIHFS_BLOCK;
constexpr uint32_t IPB = B / sizeof(FihfsInode);
constexpr uint32_t DPB = B / sizeof(FihfsDirent);
constexpr uint32_t BITS_PER_BLOCK = B * 8;
const uint8_t zero_block[B] = {};

uint32_t checksum(const FihfsSuper& sb) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&sb);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < offsetof(FihfsSuper, checksum); i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

uint32_t now(Fihfs* fs) { return fs->dev.now ? fs->dev.now(fs->dev.ctx) : 0; }

int rd(Fihfs* fs, uint32_t b, void* buf) {
    if (b >= fs->sb.total_blocks) return -FIH_ECORRUPT;
    return fs->dev.read(fs->dev.ctx, b, buf);
}
int wr(Fihfs* fs, uint32_t b, const void* buf) {
    if (b == 0 || b >= fs->sb.total_blocks) return -FIH_ECORRUPT;
    return fs->dev.write(fs->dev.ctx, b, buf);
}

bool valid_ptr(Fihfs* fs, uint32_t b) { return b == 0 || (b >= fs->sb.data_start && b < fs->sb.total_blocks); }

// ---- inodes ----------------------------------------------------------------

int inode_get(Fihfs* fs, uint32_t ino, FihfsInode* out) {
    if (ino == 0 || ino > fs->sb.inode_count) return -FIH_ECORRUPT;
    uint32_t idx = ino - 1;
    int e = rd(fs, fs->sb.itable_start + idx / IPB, fs->ibuf);
    if (e) return e;
    memcpy(out, fs->ibuf + (idx % IPB) * sizeof(FihfsInode), sizeof(FihfsInode));
    return 0;
}

int inode_put(Fihfs* fs, uint32_t ino, const FihfsInode* in) {
    if (ino == 0 || ino > fs->sb.inode_count) return -FIH_ECORRUPT;
    uint32_t idx = ino - 1, b = fs->sb.itable_start + idx / IPB;
    int e = rd(fs, b, fs->ibuf);
    if (e) return e;
    memcpy(fs->ibuf + (idx % IPB) * sizeof(FihfsInode), in, sizeof(FihfsInode));
    return wr(fs, b, fs->ibuf);
}

// Loads an in-use inode and rejects anything that could make later code
// read outside the filesystem.
int inode_load(Fihfs* fs, uint32_t ino, FihfsInode* in) {
    int e = inode_get(fs, ino, in);
    if (e) return e;
    if (in->type != FIH_FT_FILE && in->type != FIH_FT_DIR) return -FIH_ECORRUPT;
    if (in->size > FIHFS_MAX_FILE) return -FIH_ECORRUPT;
    if (in->type == FIH_FT_DIR && in->size % sizeof(FihfsDirent)) return -FIH_ECORRUPT;
    for (uint32_t i = 0; i < FIHFS_NDIRECT; i++)
        if (!valid_ptr(fs, in->direct[i])) return -FIH_ECORRUPT;
    if (!valid_ptr(fs, in->indirect)) return -FIH_ECORRUPT;
    return 0;
}

int inode_alloc(Fihfs* fs, uint32_t type, uint32_t parent, uint32_t* out) {
    for (uint32_t bi = 0; bi < fs->sb.itable_blocks; bi++) {
        int e = rd(fs, fs->sb.itable_start + bi, fs->ibuf);
        if (e) return e;
        for (uint32_t i = 0; i < IPB; i++) {
            uint32_t ino = bi * IPB + i + 1;
            if (ino > fs->sb.inode_count) return -FIH_ENOSPC;
            auto* in = reinterpret_cast<FihfsInode*>(fs->ibuf + i * sizeof(FihfsInode));
            if (in->type != 0) continue;
            memset(in, 0, sizeof(*in));
            in->type = (uint16_t)type;
            in->ctime = in->mtime = now(fs);
            in->parent = type == FIH_FT_DIR ? parent : 0;
            e = wr(fs, fs->sb.itable_start + bi, fs->ibuf);
            if (e) return e;
            fs->free_inodes--;
            *out = ino;
            return 0;
        }
    }
    return -FIH_ENOSPC;
}

int inode_free(Fihfs* fs, uint32_t ino) {
    FihfsInode in;
    memset(&in, 0, sizeof(in));
    int e = inode_put(fs, ino, &in);
    if (!e) fs->free_inodes++;
    return e;
}

// ---- block bitmap ----------------------------------------------------------

int block_alloc(Fihfs* fs, uint32_t* out) {
    uint32_t total = fs->sb.total_blocks;
    for (uint32_t n = 0; n < fs->sb.bitmap_blocks; n++) {
        uint32_t bi = (fs->alloc_hint + n) % fs->sb.bitmap_blocks;
        int e = rd(fs, fs->sb.bitmap_start + bi, fs->bbuf);
        if (e) return e;
        for (uint32_t byte = 0; byte < B; byte++) {
            if (fs->bbuf[byte] == 0xFF) continue;
            for (uint32_t bit = 0; bit < 8; bit++) {
                uint32_t blk = bi * BITS_PER_BLOCK + byte * 8 + bit;
                if (blk >= total) break;
                if (fs->bbuf[byte] & (1u << bit) || blk < fs->sb.data_start) continue;
                // Zero first: a crash after marking can only leak a block,
                // never expose stale data through a new file.
                if ((e = wr(fs, blk, zero_block))) return e;
                fs->bbuf[byte] |= (uint8_t)(1u << bit);
                if ((e = wr(fs, fs->sb.bitmap_start + bi, fs->bbuf))) return e;
                fs->free_blocks--;
                fs->alloc_hint = bi;
                *out = blk;
                return 0;
            }
        }
    }
    return -FIH_ENOSPC;
}

int block_free(Fihfs* fs, uint32_t blk) {
    if (blk < fs->sb.data_start || blk >= fs->sb.total_blocks) return -FIH_ECORRUPT;
    uint32_t bi = blk / BITS_PER_BLOCK, off = blk % BITS_PER_BLOCK;
    int e = rd(fs, fs->sb.bitmap_start + bi, fs->bbuf);
    if (e) return e;
    if (!(fs->bbuf[off / 8] & (1u << (off % 8)))) return -FIH_ECORRUPT; // already free
    fs->bbuf[off / 8] &= (uint8_t) ~(1u << (off % 8));
    e = wr(fs, fs->sb.bitmap_start + bi, fs->bbuf);
    if (!e) fs->free_blocks++;
    return e;
}

// Maps file block `fb` to a disk block (0 = hole). Allocates when `alloc`;
// the caller must write the inode back if *dirty is set.
int bmap(Fihfs* fs, FihfsInode* in, uint32_t fb, bool alloc, uint32_t* out, bool* dirty) {
    int e;
    if (fb < FIHFS_NDIRECT) {
        if (!in->direct[fb] && alloc) {
            if ((e = block_alloc(fs, &in->direct[fb]))) return e;
            *dirty = true;
        }
        *out = in->direct[fb];
        return 0;
    }
    fb -= FIHFS_NDIRECT;
    if (fb >= FIHFS_PER_IND) return -FIH_EFBIG;
    if (!in->indirect) {
        if (!alloc) { *out = 0; return 0; }
        if ((e = block_alloc(fs, &in->indirect))) return e;
        *dirty = true;
    }
    if ((e = rd(fs, in->indirect, fs->ind))) return e;
    auto* t = reinterpret_cast<uint32_t*>(fs->ind);
    uint32_t b = t[fb];
    if (!valid_ptr(fs, b)) return -FIH_ECORRUPT;
    if (!b && alloc) {
        if ((e = block_alloc(fs, &b))) return e;
        t[fb] = b;
        if ((e = wr(fs, in->indirect, fs->ind))) return e;
    }
    *out = b;
    return 0;
}

// ---- file data -------------------------------------------------------------

int file_read(Fihfs* fs, FihfsInode* in, uint32_t off, void* buf, uint32_t n) {
    if (off >= in->size) return 0;
    if (n > in->size - off) n = in->size - off;
    auto* dst = static_cast<uint8_t*>(buf);
    uint32_t done = 0;
    bool dirty = false;
    while (done < n) {
        uint32_t pos = off + done, bo = pos % B, chunk = B - bo;
        if (chunk > n - done) chunk = n - done;
        uint32_t pb;
        int e = bmap(fs, in, pos / B, false, &pb, &dirty);
        if (e) return done ? (int)done : e;
        if (!pb) {
            memset(dst + done, 0, chunk);
        } else {
            if ((e = rd(fs, pb, fs->data))) return done ? (int)done : e;
            memcpy(dst + done, fs->data + bo, chunk);
        }
        done += chunk;
    }
    return (int)done;
}

int file_truncate(Fihfs* fs, uint32_t ino, FihfsInode* in, uint32_t size) {
    if (size > FIHFS_MAX_FILE) return -FIH_EFBIG;
    int e;
    bool dirty = false;
    if (size >= in->size) {
        in->size = size;
        in->mtime = now(fs);
        return inode_put(fs, ino, in);
    }
    // Clear the tail of the last kept block so growing the file later
    // never resurrects old bytes.
    if (size % B) {
        uint32_t pb;
        if ((e = bmap(fs, in, size / B, false, &pb, &dirty))) return e;
        if (pb) {
            if ((e = rd(fs, pb, fs->data))) return e;
            memset(fs->data + size % B, 0, B - size % B);
            if ((e = wr(fs, pb, fs->data))) return e;
        }
    }
    uint32_t keep = (size + B - 1) / B;
    uint32_t direct_free[FIHFS_NDIRECT];
    uint32_t ndirect = 0;
    for (uint32_t i = keep; i < FIHFS_NDIRECT; i++) {
        if (in->direct[i]) direct_free[ndirect++] = in->direct[i];
        in->direct[i] = 0;
    }
    uint32_t drop_ind = 0, ind_from = keep > FIHFS_NDIRECT ? keep - FIHFS_NDIRECT : 0;
    bool have_ind = in->indirect != 0;
    if (have_ind) {
        if ((e = rd(fs, in->indirect, fs->ind))) return e;
        memcpy(fs->data, fs->ind, B); // old table, used for freeing below
        if (keep <= FIHFS_NDIRECT) {
            drop_ind = in->indirect;
            in->indirect = 0;
        } else {
            memset(fs->ind + ind_from * 4, 0, (FIHFS_PER_IND - ind_from) * 4);
            if ((e = wr(fs, in->indirect, fs->ind))) return e;
        }
    }
    in->size = size;
    in->mtime = now(fs);
    // The inode no longer references the blocks before any of them is freed.
    if ((e = inode_put(fs, ino, in))) return e;
    for (uint32_t i = 0; i < ndirect; i++)
        if ((e = block_free(fs, direct_free[i]))) return e;
    if (have_ind) {
        auto* old = reinterpret_cast<uint32_t*>(fs->data);
        for (uint32_t i = ind_from; i < FIHFS_PER_IND; i++)
            if (old[i] && (e = block_free(fs, old[i]))) return e;
        if (drop_ind && (e = block_free(fs, drop_ind))) return e;
    }
    return 0;
}

// ---- directories -----------------------------------------------------------

bool name_ok(const char* name, size_t len) {
    if (len == 0) return false;
    if ((len == 1 && name[0] == '.') || (len == 2 && name[0] == '.' && name[1] == '.')) return false;
    for (size_t i = 0; i < len; i++)
        if (name[i] == '/' || name[i] == 0) return false;
    return true;
}

// Reads the block holding directory slot `slot` into dbuf. *pb = 0 for a hole.
int dir_block(Fihfs* fs, FihfsInode* dir, uint32_t slot, uint32_t* pb) {
    bool dirty = false;
    int e = bmap(fs, dir, slot / DPB, false, pb, &dirty);
    if (e || !*pb) return e;
    return rd(fs, *pb, fs->dbuf);
}

FihfsDirent* slot_ptr(Fihfs* fs, uint32_t slot) {
    return reinterpret_cast<FihfsDirent*>(fs->dbuf) + slot % DPB;
}

// Visits every used entry. fn returns true to stop; *stop_slot receives it.
template <typename Fn> int dir_scan(Fihfs* fs, FihfsInode* dir, Fn fn) {
    uint32_t n = dir->size / sizeof(FihfsDirent), pb = 0;
    for (uint32_t s = 0; s < n; s++) {
        if (s % DPB == 0) {
            int e = dir_block(fs, dir, s, &pb);
            if (e) return e;
        }
        if (!pb) continue;
        FihfsDirent* d = slot_ptr(fs, s);
        if (!d->ino) continue;
        if (d->ino > fs->sb.inode_count || d->name_len == 0 || d->name_len > FIH_NAME_MAX) return -FIH_ECORRUPT;
        if (fn(s, d)) return 1;
    }
    return 0;
}

int dir_find(Fihfs* fs, FihfsInode* dir, const char* name, size_t len, uint32_t* ino, uint32_t* slot) {
    int r = dir_scan(fs, dir, [&](uint32_t s, FihfsDirent* d) {
        if (d->name_len != len || memcmp(d->name, name, len) != 0) return false;
        *ino = d->ino;
        if (slot) *slot = s;
        return true;
    });
    if (r < 0) return r;
    return r ? 0 : -FIH_ENOENT;
}

int dir_add(Fihfs* fs, uint32_t dir_ino, FihfsInode* dir, const char* name, size_t len, uint32_t ino, uint32_t type) {
    uint32_t n = dir->size / sizeof(FihfsDirent), slot = n, pb = 0;
    for (uint32_t s = 0; s < n; s++) {
        if (s % DPB == 0) {
            int e = dir_block(fs, dir, s, &pb);
            if (e) return e;
        }
        if (pb && !slot_ptr(fs, s)->ino) { slot = s; break; }
    }
    bool dirty = false;
    int e = bmap(fs, dir, slot / DPB, true, &pb, &dirty);
    if (e) return e;
    if ((e = rd(fs, pb, fs->dbuf))) return e;
    FihfsDirent* d = slot_ptr(fs, slot);
    memset(d, 0, sizeof(*d));
    d->ino = ino;
    d->type = (uint8_t)type;
    d->name_len = (uint8_t)len;
    memcpy(d->name, name, len);
    if ((e = wr(fs, pb, fs->dbuf))) return e;
    if (slot == n) dir->size += sizeof(FihfsDirent);
    dir->mtime = now(fs);
    return inode_put(fs, dir_ino, dir);
}

int dir_set_slot(Fihfs* fs, FihfsInode* dir, uint32_t slot, uint32_t ino) {
    uint32_t pb;
    int e = dir_block(fs, dir, slot, &pb);
    if (e) return e;
    if (!pb) return -FIH_ECORRUPT;
    FihfsDirent* d = slot_ptr(fs, slot);
    if (ino) d->ino = ino;
    else memset(d, 0, sizeof(*d));
    return wr(fs, pb, fs->dbuf);
}

int walk(Fihfs* fs, const char* path, uint32_t* out) {
    if (path[0] != '/') return -FIH_EINVAL;
    uint32_t ino = FIHFS_ROOT;
    const char* p = path;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char* end = p;
        while (*end && *end != '/') end++;
        size_t len = (size_t)(end - p);
        if (len > FIH_NAME_MAX) return -FIH_ENAMETOOLONG;
        FihfsInode dir;
        int e = inode_load(fs, ino, &dir);
        if (e) return e;
        if (dir.type != FIH_FT_DIR) return -FIH_ENOTDIR;
        if ((e = dir_find(fs, &dir, p, len, &ino, nullptr))) return e;
        p = end;
    }
    *out = ino;
    return 0;
}

// Splits "/a/b/c" into the inode of "/a/b" (must be a directory) and "c".
int walk_parent(Fihfs* fs, const char* path, uint32_t* parent, FihfsInode* pin, const char** name, size_t* len) {
    size_t n = strlen(path);
    while (n > 1 && path[n - 1] == '/') n--;
    size_t i = n;
    while (i > 0 && path[i - 1] != '/') i--;
    if (i == 0 || i == n) return -FIH_EINVAL; // relative path or "/"
    *name = path + i;
    *len = n - i;
    if (*len > FIH_NAME_MAX) return -FIH_ENAMETOOLONG;
    if (!name_ok(*name, *len)) return -FIH_EINVAL;
    char ppath[FIH_PATH_MAX];
    if (i >= sizeof(ppath)) return -FIH_ENAMETOOLONG;
    memcpy(ppath, path, i);
    ppath[i] = 0;
    int e = walk(fs, ppath, parent);
    if (e) return e;
    if ((e = inode_load(fs, *parent, pin))) return e;
    return pin->type == FIH_FT_DIR ? 0 : -FIH_ENOTDIR;
}
}

// ---- public API ------------------------------------------------------------

int fihfs_mkfs(const FihfsDev* dev, const char* label) {
    uint32_t total = dev->blocks;
    if (total < 32) return -FIH_EINVAL;
    FihfsSuper sb;
    memset(&sb, 0, sizeof(sb));
    memcpy(sb.magic, MAGIC, 8);
    sb.version = FIHFS_VERSION;
    sb.block_size = B;
    sb.total_blocks = total;
    uint32_t inodes = total / 4 < 64 ? 64 : total / 4;
    sb.inode_count = (inodes + IPB - 1) / IPB * IPB;
    sb.bitmap_start = 1;
    sb.bitmap_blocks = (total + BITS_PER_BLOCK - 1) / BITS_PER_BLOCK;
    sb.itable_start = sb.bitmap_start + sb.bitmap_blocks;
    sb.itable_blocks = sb.inode_count / IPB;
    sb.data_start = sb.itable_start + sb.itable_blocks;
    if (sb.data_start + 8 > total) return -FIH_EINVAL;
    sb.created = dev->now ? dev->now(dev->ctx) : 0;
    if (label) {
        size_t l = strlen(label);
        memcpy(sb.label, label, l < sizeof(sb.label) - 1 ? l : sizeof(sb.label) - 1);
    }
    sb.checksum = checksum(sb);

    static uint8_t buf[B];
    int e;
    // Invalidate any old superblock first so a half-written image never mounts.
    if ((e = dev->write(dev->ctx, 0, zero_block))) return e;
    for (uint32_t i = 0; i < sb.bitmap_blocks; i++) {
        memset(buf, 0, B);
        for (uint32_t blk = i * BITS_PER_BLOCK; blk < (i + 1) * BITS_PER_BLOCK && blk < total; blk++) {
            uint32_t off = blk - i * BITS_PER_BLOCK;
            if (blk < sb.data_start) buf[off / 8] |= (uint8_t)(1u << (off % 8));
        }
        if (i == sb.bitmap_blocks - 1) // bits past the end stay set: never allocatable
            for (uint32_t off = total - i * BITS_PER_BLOCK; off < BITS_PER_BLOCK; off++)
                buf[off / 8] |= (uint8_t)(1u << (off % 8));
        if ((e = dev->write(dev->ctx, sb.bitmap_start + i, buf))) return e;
    }
    for (uint32_t i = 0; i < sb.itable_blocks; i++) {
        memset(buf, 0, B);
        if (i == 0) {
            auto* root = reinterpret_cast<FihfsInode*>(buf);
            root->type = FIH_FT_DIR;
            root->ctime = root->mtime = sb.created;
            root->parent = FIHFS_ROOT;
        }
        if ((e = dev->write(dev->ctx, sb.itable_start + i, buf))) return e;
    }
    memset(buf, 0, B);
    memcpy(buf, &sb, sizeof(sb));
    return dev->write(dev->ctx, 0, buf);
}

int fihfs_mount(Fihfs* fs, const FihfsDev* dev) {
    fs->dev = *dev;
    int e = dev->read(dev->ctx, 0, fs->data);
    if (e) return e;
    memcpy(&fs->sb, fs->data, sizeof(fs->sb));
    const FihfsSuper& sb = fs->sb;
    if (memcmp(sb.magic, MAGIC, 8) != 0) return -FIH_ENODEV; // not a FihFS volume
    if (sb.checksum != checksum(sb) || sb.version != FIHFS_VERSION || sb.block_size != B ||
        sb.total_blocks > dev->blocks || sb.total_blocks < 32 || sb.bitmap_start != 1 ||
        sb.bitmap_blocks != (sb.total_blocks + BITS_PER_BLOCK - 1) / BITS_PER_BLOCK ||
        sb.itable_start != 1 + sb.bitmap_blocks || sb.inode_count == 0 || sb.inode_count % IPB ||
        sb.itable_blocks != sb.inode_count / IPB || sb.data_start != sb.itable_start + sb.itable_blocks ||
        sb.data_start >= sb.total_blocks)
        return -FIH_ECORRUPT;
    fs->free_blocks = 0;
    for (uint32_t bi = 0; bi < sb.bitmap_blocks; bi++) {
        if ((e = rd(fs, sb.bitmap_start + bi, fs->bbuf))) return e;
        for (uint32_t off = 0; off < BITS_PER_BLOCK; off++) {
            uint32_t blk = bi * BITS_PER_BLOCK + off;
            if (blk >= sb.total_blocks) break;
            if (!(fs->bbuf[off / 8] & (1u << (off % 8)))) fs->free_blocks++;
        }
    }
    fs->free_inodes = 0;
    for (uint32_t bi = 0; bi < sb.itable_blocks; bi++) {
        if ((e = rd(fs, sb.itable_start + bi, fs->ibuf))) return e;
        for (uint32_t i = 0; i < IPB; i++)
            if (reinterpret_cast<FihfsInode*>(fs->ibuf)[i].type == 0) fs->free_inodes++;
    }
    fs->alloc_hint = 0;
    FihfsInode root;
    if ((e = inode_load(fs, FIHFS_ROOT, &root))) return e;
    return root.type == FIH_FT_DIR ? 0 : -FIH_ECORRUPT;
}

int fihfs_lookup(Fihfs* fs, const char* path, uint32_t* ino) { return walk(fs, path, ino); }

int fihfs_stat(Fihfs* fs, uint32_t ino, fih_stat_t* st) {
    FihfsInode in;
    int e = inode_load(fs, ino, &in);
    if (e) return e;
    st->type = in.type;
    st->size = in.size;
    st->mtime = in.mtime;
    st->ctime = in.ctime;
    st->inode = ino;
    st->flags = 0;
    return 0;
}

int fihfs_read(Fihfs* fs, uint32_t ino, uint32_t off, void* buf, uint32_t n) {
    FihfsInode in;
    int e = inode_load(fs, ino, &in);
    if (e) return e;
    if (in.type != FIH_FT_FILE) return -FIH_EISDIR;
    return file_read(fs, &in, off, buf, n);
}

int fihfs_write(Fihfs* fs, uint32_t ino, uint32_t off, const void* buf, uint32_t n) {
    FihfsInode in;
    int e = inode_load(fs, ino, &in);
    if (e) return e;
    if (in.type != FIH_FT_FILE) return -FIH_EISDIR;
    if (n == 0) return 0;
    if (off >= FIHFS_MAX_FILE) return -FIH_EFBIG;
    if (n > FIHFS_MAX_FILE - off) n = FIHFS_MAX_FILE - off;
    auto* src = static_cast<const uint8_t*>(buf);
    uint32_t done = 0;
    bool dirty = false;
    while (done < n) {
        uint32_t pos = off + done, bo = pos % B, chunk = B - bo;
        if (chunk > n - done) chunk = n - done;
        uint32_t pb;
        if ((e = bmap(fs, &in, pos / B, true, &pb, &dirty))) break;
        if (chunk == B) {
            e = wr(fs, pb, src + done);
        } else {
            if (!(e = rd(fs, pb, fs->data))) {
                memcpy(fs->data + bo, src + done, chunk);
                e = wr(fs, pb, fs->data);
            }
        }
        if (e) break;
        done += chunk;
    }
    if (off + done > in.size) in.size = off + done;
    in.mtime = now(fs);
    int pe = inode_put(fs, ino, &in);
    if (done) return (int)done;
    return e ? e : pe;
}

int fihfs_truncate(Fihfs* fs, uint32_t ino, uint32_t size) {
    FihfsInode in;
    int e = inode_load(fs, ino, &in);
    if (e) return e;
    if (in.type != FIH_FT_FILE) return -FIH_EISDIR;
    return file_truncate(fs, ino, &in, size);
}

int fihfs_create(Fihfs* fs, const char* path, uint32_t type, uint32_t* out) {
    if (type != FIH_FT_FILE && type != FIH_FT_DIR) return -FIH_EINVAL;
    uint32_t parent, ino;
    FihfsInode pin;
    const char* name;
    size_t len;
    int e = walk_parent(fs, path, &parent, &pin, &name, &len);
    if (e) return e;
    e = dir_find(fs, &pin, name, len, &ino, nullptr);
    if (e == 0) return -FIH_EEXIST;
    if (e != -FIH_ENOENT) return e;
    if ((e = inode_alloc(fs, type, parent, &ino))) return e;
    if ((e = dir_add(fs, parent, &pin, name, len, ino, type))) {
        inode_free(fs, ino);
        return e;
    }
    if (out) *out = ino;
    return 0;
}

int fihfs_unlink(Fihfs* fs, const char* path) {
    uint32_t parent, ino, slot;
    FihfsInode pin, in;
    const char* name;
    size_t len;
    int e = walk_parent(fs, path, &parent, &pin, &name, &len);
    if (e) return e == -FIH_EINVAL && strcmp(path, "/") == 0 ? -FIH_EBUSY : e;
    if ((e = dir_find(fs, &pin, name, len, &ino, &slot))) return e;
    if ((e = inode_load(fs, ino, &in))) return e;
    if (in.type == FIH_FT_DIR) {
        int r = dir_scan(fs, &in, [](uint32_t, FihfsDirent*) { return true; });
        if (r < 0) return r;
        if (r) return -FIH_ENOTEMPTY;
    }
    // Unlink first: a crash afterwards leaves an orphan, never a dangling entry.
    if ((e = dir_set_slot(fs, &pin, slot, 0))) return e;
    pin.mtime = now(fs);
    inode_put(fs, parent, &pin);
    if ((e = file_truncate(fs, ino, &in, 0))) return e;
    return inode_free(fs, ino);
}

int fihfs_rename(Fihfs* fs, const char* from, const char* to, int flags) {
    uint32_t sparent, dparent, sino, sslot, dino, dslot;
    FihfsInode spin, dpin, sin;
    const char *sname, *dname;
    size_t slen, dlen;
    int e = walk_parent(fs, from, &sparent, &spin, &sname, &slen);
    if (e) return e;
    if ((e = dir_find(fs, &spin, sname, slen, &sino, &sslot))) return e;
    if ((e = walk_parent(fs, to, &dparent, &dpin, &dname, &dlen))) return e;
    if ((e = inode_load(fs, sino, &sin))) return e;

    bool dst_exists = false;
    e = dir_find(fs, &dpin, dname, dlen, &dino, &dslot);
    if (e == 0) {
        if (dino == sino) return 0;
        FihfsInode din;
        if ((e = inode_load(fs, dino, &din))) return e;
        if (!(flags & FIH_RENAME_REPLACE)) return -FIH_EEXIST;
        if (din.type != FIH_FT_FILE || sin.type != FIH_FT_FILE) return -FIH_EEXIST;
        dst_exists = true;
    } else if (e != -FIH_ENOENT) {
        return e;
    }

    if (sin.type == FIH_FT_DIR) { // refuse to move a directory into itself
        uint32_t cur = dparent;
        for (uint32_t depth = 0; depth < fs->sb.inode_count; depth++) {
            if (cur == sino) return -FIH_EINVAL;
            if (cur == FIHFS_ROOT) break;
            FihfsInode c;
            if ((e = inode_load(fs, cur, &c))) return e;
            cur = c.parent;
        }
    }

    if (dst_exists) {
        // One block write switches the name to the new inode.
        if ((e = dir_set_slot(fs, &dpin, dslot, sino))) return e;
        if (dparent == sparent) spin = dpin;
        if ((e = dir_set_slot(fs, &spin, sslot, 0))) return e;
        FihfsInode din;
        if ((e = inode_load(fs, dino, &din))) return e;
        if ((e = file_truncate(fs, dino, &din, 0))) return e;
        return inode_free(fs, dino);
    }
    if (sparent == dparent) {
        uint32_t pb;
        if ((e = dir_block(fs, &spin, sslot, &pb))) return e;
        if (!pb) return -FIH_ECORRUPT;
        FihfsDirent* d = slot_ptr(fs, sslot);
        memset(d->name, 0, sizeof(d->name));
        memcpy(d->name, dname, dlen);
        d->name_len = (uint8_t)dlen;
        return wr(fs, pb, fs->dbuf);
    }
    if ((e = dir_add(fs, dparent, &dpin, dname, dlen, sino, sin.type))) return e;
    if ((e = dir_set_slot(fs, &spin, sslot, 0))) return e;
    spin.mtime = now(fs);
    inode_put(fs, sparent, &spin);
    if (sin.type == FIH_FT_DIR) {
        sin.parent = dparent;
        return inode_put(fs, sino, &sin);
    }
    return 0;
}

int fihfs_readdir(Fihfs* fs, uint32_t ino, uint32_t index, fih_dirent_t* out) {
    FihfsInode dir;
    int e = inode_load(fs, ino, &dir);
    if (e) return e;
    if (dir.type != FIH_FT_DIR) return -FIH_ENOTDIR;
    uint32_t n = 0, child = 0;
    int r = dir_scan(fs, &dir, [&](uint32_t, FihfsDirent* d) {
        if (n++ != index) return false;
        memset(out, 0, sizeof(*out));
        memcpy(out->name, d->name, d->name_len);
        out->type = d->type;
        child = d->ino;
        return true;
    });
    if (r <= 0) return r;
    FihfsInode cin;
    if (inode_get(fs, child, &cin) == 0) out->size = cin.size;
    return 1;
}

int fihfs_statfs(Fihfs* fs, fih_statfs_t* out) {
    memset(out, 0, sizeof(*out));
    out->block_size = B;
    out->total_blocks = fs->sb.total_blocks;
    out->free_blocks = fs->free_blocks;
    out->total_inodes = fs->sb.inode_count;
    out->free_inodes = fs->free_inodes;
    memcpy(out->fstype, "fihfs", 6);
    return 0;
}

#ifndef FIH_KERNEL
int fihfs_check(Fihfs* fs, void (*report)(void*, const char*), void* ctx) {
    const FihfsSuper& sb = fs->sb;
    uint8_t* bref = (uint8_t*)calloc(sb.total_blocks, 1);
    uint8_t* iref = (uint8_t*)calloc(sb.inode_count + 1, 1);
    uint32_t* stack = (uint32_t*)malloc(sizeof(uint32_t) * (sb.inode_count + 1));
    int problems = 0;
    char msg[160];
    auto bad = [&](const char* m) { problems++; if (report) report(ctx, m); };
    auto ref = [&](uint32_t b, uint32_t ino) {
        if (!b) return;
        if (b < sb.data_start || b >= sb.total_blocks) {
            snprintf(msg, sizeof msg, "inode %u: block pointer %u out of range", ino, b);
            bad(msg);
        } else if (bref[b]++) {
            snprintf(msg, sizeof msg, "block %u referenced more than once (inode %u)", b, ino);
            bad(msg);
        }
    };
    uint32_t sp = 0;
    stack[sp++] = FIHFS_ROOT;
    iref[FIHFS_ROOT] = 1;
    while (sp) {
        uint32_t ino = stack[--sp];
        FihfsInode in;
        if (inode_load(fs, ino, &in)) {
            snprintf(msg, sizeof msg, "inode %u: invalid or unreadable", ino);
            bad(msg);
            continue;
        }
        for (uint32_t i = 0; i < FIHFS_NDIRECT; i++) ref(in.direct[i], ino);
        if (in.indirect) {
            ref(in.indirect, ino);
            uint8_t tbl[B];
            if (rd(fs, in.indirect, tbl) == 0)
                for (uint32_t i = 0; i < FIHFS_PER_IND; i++) ref(reinterpret_cast<uint32_t*>(tbl)[i], ino);
        }
        if (in.type != FIH_FT_DIR) continue;
        int r = dir_scan(fs, &in, [&](uint32_t, FihfsDirent* d) {
            FihfsInode c;
            if (inode_get(fs, d->ino, &c) || c.type == 0) {
                snprintf(msg, sizeof msg, "dir %u: entry '%.*s' points to free inode %u", ino, d->name_len, d->name, d->ino);
                bad(msg);
                return false;
            }
            if (c.type != d->type) {
                snprintf(msg, sizeof msg, "dir %u: entry '%.*s' type mismatch", ino, d->name_len, d->name);
                bad(msg);
            }
            if (iref[d->ino]) {
                snprintf(msg, sizeof msg, "inode %u linked more than once", d->ino);
                bad(msg);
                return false;
            }
            if (c.type == FIH_FT_DIR && c.parent != ino) {
                snprintf(msg, sizeof msg, "dir %u: parent field %u, expected %u", d->ino, c.parent, ino);
                bad(msg);
            }
            iref[d->ino] = 1;
            stack[sp++] = d->ino;
            return false;
        });
        if (r < 0) {
            snprintf(msg, sizeof msg, "dir %u: corrupt entries", ino);
            bad(msg);
        }
    }
    for (uint32_t ino = 1; ino <= sb.inode_count; ino++) {
        FihfsInode in;
        if (inode_get(fs, ino, &in) == 0 && in.type != 0 && !iref[ino]) {
            snprintf(msg, sizeof msg, "orphan inode %u (type %u, %u bytes)", ino, in.type, in.size);
            bad(msg);
        }
    }
    uint32_t free_seen = 0;
    for (uint32_t b = 0; b < sb.total_blocks; b++) {
        if (b % BITS_PER_BLOCK == 0) rd(fs, sb.bitmap_start + b / BITS_PER_BLOCK, fs->bbuf);
        uint32_t off = b % BITS_PER_BLOCK;
        bool set = fs->bbuf[off / 8] & (1u << (off % 8));
        if (!set) free_seen++;
        if (b < sb.data_start) {
            if (!set) { snprintf(msg, sizeof msg, "metadata block %u marked free", b); bad(msg); }
        } else if (bref[b] && !set) {
            snprintf(msg, sizeof msg, "block %u in use but marked free", b);
            bad(msg);
        } else if (!bref[b] && set) {
            snprintf(msg, sizeof msg, "block %u leaked (allocated, unreferenced)", b);
            bad(msg);
        }
    }
    if (free_seen != fs->free_blocks) {
        snprintf(msg, sizeof msg, "free block count %u, bitmap says %u", fs->free_blocks, free_seen);
        bad(msg);
    }
    free(bref);
    free(iref);
    free(stack);
    return problems;
}
#endif
