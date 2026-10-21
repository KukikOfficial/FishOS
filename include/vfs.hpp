#pragma once
#include <stdint.h>

constexpr int MAX_FILES = 12;
constexpr int MAX_FILE_SIZE = 1536;

struct VFSFile {
    char name[32];
    uint32_t size;
    char content[MAX_FILE_SIZE];
    bool used;
};

extern VFSFile vfs[MAX_FILES];
extern int vfs_file_count;

void vfs_init(uint32_t mod_start, uint32_t mod_end);
void vfs_add_file(const char* name, const char* data, uint32_t size);
void vfs_save_file(const char* name, const char* data, uint32_t size);
void vfs_delete_file(int index);
void vfs_sync_to_disk();