#pragma once
#include <stdint.h>
#include <stddef.h>

enum NodeType {
    FS_FILE = 0,
    FS_DIRECTORY = 1
};

struct vfs_node_t {
    char name[64];
    NodeType type;
    uint32_t size;
    char* content;

    vfs_node_t* parent;
    vfs_node_t* first_child;
    vfs_node_t* next_sibling;
};

// VFS Core API
void vfs_init(uint32_t mod_start, uint32_t mod_end);
vfs_node_t* vfs_get_root();
vfs_node_t* vfs_get_current_dir();
void vfs_set_current_dir(vfs_node_t* dir);

vfs_node_t* vfs_lookup(const char* path);
vfs_node_t* vfs_mkdir(const char* path);
vfs_node_t* vfs_create_file(const char* path);
bool vfs_write_file(const char* path, const char* data, uint32_t size);
bool vfs_delete_node(vfs_node_t* node);
void vfs_sync_to_disk();

// Legacy / GUI compatibility helpers
constexpr int MAX_GUI_ITEMS = 32;
extern int vfs_file_count;
struct VFSGuiItem {
    char name[64];
    bool is_dir;
    uint32_t size;
    vfs_node_t* node_ptr;
};
extern VFSGuiItem vfs_gui_list[MAX_GUI_ITEMS];
void vfs_refresh_gui_list();