// Virtual filesystem: mount table, path normalization and open files.
#pragma once
#include <stdint.h>
#include <fihcore/abi.h>

struct Fs;
struct FsOps {
    int (*lookup)(Fs*, const char* path, uint32_t* ino);
    int (*stat)(Fs*, uint32_t ino, fih_stat_t* st);
    int (*read)(Fs*, uint32_t ino, uint32_t off, void* buf, uint32_t n);
    int (*write)(Fs*, uint32_t ino, uint32_t off, const void* buf, uint32_t n);
    int (*truncate)(Fs*, uint32_t ino, uint32_t size);
    int (*create)(Fs*, const char* path, uint32_t type, uint32_t* ino);
    int (*unlink)(Fs*, const char* path);
    int (*rename)(Fs*, const char* from, const char* to, int flags);
    int (*readdir)(Fs*, uint32_t ino, uint32_t index, fih_dirent_t* out);
    int (*statfs)(Fs*, fih_statfs_t* out);
};

struct Fs {
    const FsOps* ops;
    void* priv;
    bool readonly;
    char device[32];
};

struct Pipe;
struct File {
    uint32_t refs;
    uint32_t type; // FIH_FT_*
    uint32_t flags;
    Fs* fs;
    uint32_t ino;
    uint32_t pos;
    Pipe* pipe;
};

void vfs_init();
int vfs_mount(const char* path, Fs* fs);
Fs* fihfs_open_device(int ata_disk); // nullptr unless the disk holds a valid FihFS
Fs* tarfs_create(const uint8_t* data, uint32_t size);

// Resolves `path` (absolute or relative to `cwd`) into a normalized absolute path.
int vfs_normalize(const char* cwd, const char* path, char out[FIH_PATH_MAX]);

int vfs_open(const char* abs, uint32_t flags, File** out);
File* vfs_console();
File* file_ref(File* f);
void file_close(File* f);
int file_read(File* f, void* buf, uint32_t n);
int file_write(File* f, const void* buf, uint32_t n);
int file_seek(File* f, int32_t off, int whence);
int file_stat(File* f, fih_stat_t* st);
int file_readdir(File* f, uint32_t index, fih_dirent_t* out);
int file_truncate(File* f, uint32_t size);
int vfs_stat(const char* abs, fih_stat_t* st);
int vfs_mkdir(const char* abs);
int vfs_unlink(const char* abs);
int vfs_rename(const char* from, const char* to, int flags);
int vfs_statfs(const char* abs, fih_statfs_t* out);

int pipe_create(uint32_t flags, File** rd, File** wr);
