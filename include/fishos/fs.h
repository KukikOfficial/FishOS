// File and path helpers shared by FishOS programs.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <fihcore/abi.h>

namespace fs {
// Makes `path` absolute (relative to the working directory) and normalized.
int absolute(const char* path, char* out, size_t size);
// out = dir + "/" + name (normalized).
int join(const char* dir, const char* name, char* out, size_t size);
const char* basename(const char* path);
void dirname(const char* path, char* out, size_t size);

// Reads a whole file into a malloc'd, NUL-terminated buffer.
int read_all(const char* path, char** data, uint32_t* len, uint32_t max = 4 * 1024 * 1024);
// Writes through a temporary file and an atomic rename, so an interrupted
// save never leaves a half-written document behind.
int write_atomic(const char* path, const char* data, uint32_t len);
// Copies a file or (recursively) a directory. Refuses to copy into itself.
int copy(const char* from, const char* to);
// Deletes a file or a directory tree.
int remove_tree(const char* path);
bool exists(const char* path);
bool is_dir(const char* path);
}
