// FihFS: the native Fihcore disk filesystem.
//
// Layout (4 KiB blocks):
//   block 0                superblock
//   bitmap_start..         block allocation bitmap (1 bit per block)
//   itable_start..         inode table, 32 inodes of 128 bytes per block
//   data_start..           file and directory data
// Inode 1 is the root directory. Directories hold 64-byte entries.
// Each file has 12 direct blocks and one single-indirect block (max ~4 MiB).
//
// This file is compiled both into the kernel and into host tools/tests, so it
// depends only on the block device callbacks below and on memcpy/memset.
#pragma once
#include <stdint.h>
#include <fihcore/abi.h>

constexpr uint32_t FIHFS_BLOCK = 4096;
constexpr uint32_t FIHFS_VERSION = 1;
constexpr uint32_t FIHFS_NDIRECT = 12;
constexpr uint32_t FIHFS_PER_IND = FIHFS_BLOCK / 4;
constexpr uint32_t FIHFS_MAX_FILE = (FIHFS_NDIRECT + FIHFS_PER_IND) * FIHFS_BLOCK;
constexpr uint32_t FIHFS_ROOT = 1;

struct FihfsSuper {
    char magic[8]; // "FIHFS\0v1"
    uint32_t version;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t inode_count;
    uint32_t bitmap_start, bitmap_blocks;
    uint32_t itable_start, itable_blocks;
    uint32_t data_start;
    uint32_t created;
    char label[32];
    uint32_t checksum; // over all preceding bytes
};

struct FihfsInode {
    uint16_t type; // 0 free, FIH_FT_FILE, FIH_FT_DIR
    uint16_t reserved0;
    uint32_t size;
    uint32_t ctime, mtime;
    uint32_t parent; // directories only
    uint32_t direct[FIHFS_NDIRECT];
    uint32_t indirect;
    uint32_t reserved[14];
};
static_assert(sizeof(FihfsInode) == 128, "inode size is part of the on-disk format");

struct FihfsDirent {
    uint32_t ino; // 0 = unused slot
    uint8_t type;
    uint8_t name_len;
    char name[58];
};
static_assert(sizeof(FihfsDirent) == 64, "dirent size is part of the on-disk format");

struct FihfsDev {
    void* ctx;
    uint32_t blocks; // device capacity in 4 KiB blocks
    int (*read)(void* ctx, uint32_t block, void* buf);
    int (*write)(void* ctx, uint32_t block, const void* buf);
    uint32_t (*now)(void* ctx);
};

struct Fihfs {
    FihfsDev dev;
    FihfsSuper sb;
    uint32_t free_blocks, free_inodes;
    uint32_t alloc_hint;
    // Scratch buffers; each is used by exactly one helper so nesting is safe.
    uint8_t data[FIHFS_BLOCK], ind[FIHFS_BLOCK], ibuf[FIHFS_BLOCK], dbuf[FIHFS_BLOCK], bbuf[FIHFS_BLOCK];
};

int fihfs_mkfs(const FihfsDev* dev, const char* label);
int fihfs_mount(Fihfs* fs, const FihfsDev* dev);

// Paths are absolute within the filesystem ("/", "/a/b"), already normalized.
int fihfs_lookup(Fihfs* fs, const char* path, uint32_t* ino);
int fihfs_stat(Fihfs* fs, uint32_t ino, fih_stat_t* st);
int fihfs_read(Fihfs* fs, uint32_t ino, uint32_t off, void* buf, uint32_t n);
int fihfs_write(Fihfs* fs, uint32_t ino, uint32_t off, const void* buf, uint32_t n);
int fihfs_truncate(Fihfs* fs, uint32_t ino, uint32_t size);
int fihfs_create(Fihfs* fs, const char* path, uint32_t type, uint32_t* ino);
int fihfs_unlink(Fihfs* fs, const char* path);
int fihfs_rename(Fihfs* fs, const char* from, const char* to, int flags);
int fihfs_readdir(Fihfs* fs, uint32_t ino, uint32_t index, fih_dirent_t* out);
int fihfs_statfs(Fihfs* fs, fih_statfs_t* out);

// Consistency check. Returns the number of problems found (0 = clean) or a
// negative error; `report` receives one line per problem.
int fihfs_check(Fihfs* fs, void (*report)(void* ctx, const char* msg), void* ctx);
