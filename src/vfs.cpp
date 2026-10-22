#include "vfs.hpp"
#include "memory.hpp"
#include "drivers.hpp"
#include "string.hpp"
#include "io.hpp"

struct tar_header {
    char filename[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag[1];
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};

static vfs_node_t* root_node = nullptr;
static vfs_node_t* current_dir = nullptr;

int vfs_file_count = 0;
VFSGuiItem vfs_gui_list[MAX_GUI_ITEMS];

static uint32_t oct2bin(const char* str, int size) {
    uint32_t n = 0;
    for (int i = 0; i < size; i++) {
        if (str[i] >= '0' && str[i] <= '7') {
            n = (n << 3) + (str[i] - '0');
        } else if (str[i] == ' ' || str[i] == '\0') {
            if (n > 0) break;
        }
    }
    return n;
}

static vfs_node_t* create_node(const char* name, NodeType type, vfs_node_t* parent) {
    auto* node = static_cast<vfs_node_t*>(kmalloc(sizeof(vfs_node_t)));
    if (!node) return nullptr;
    memset(node, 0, sizeof(vfs_node_t));

    kstrcpy(node->name, name);
    node->type = type;
    node->parent = parent;

    if (parent) {
        if (!parent->first_child) {
            parent->first_child = node;
        } else {
            vfs_node_t* sibling = parent->first_child;
            while (sibling->next_sibling) {
                sibling = sibling->next_sibling;
            }
            sibling->next_sibling = node;
        }
    }
    return node;
}

vfs_node_t* vfs_get_root() {
    return root_node;
}

vfs_node_t* vfs_get_current_dir() {
    return current_dir ? current_dir : root_node;
}

void vfs_set_current_dir(vfs_node_t* dir) {
    if (dir && dir->type == FS_DIRECTORY) {
        current_dir = dir;
        vfs_refresh_gui_list();
    }
}

static vfs_node_t* find_child(vfs_node_t* parent, const char* name) {
    if (!parent || parent->type != FS_DIRECTORY) return nullptr;
    vfs_node_t* cur = parent->first_child;
    while (cur) {
        if (kstrcmp(cur->name, name)) return cur;
        cur = cur->next_sibling;
    }
    return nullptr;
}

vfs_node_t* vfs_lookup(const char* path) {
    if (!path || !root_node) return nullptr;
    if (path[0] == '\0') return current_dir;

    vfs_node_t* cur = (path[0] == '/') ? root_node : vfs_get_current_dir();
    const char* p = (path[0] == '/') ? path + 1 : path;

    char part[64];
    while (*p) {
        int idx = 0;
        while (*p && *p != '/' && idx < 63) {
            part[idx++] = *p++;
        }
        part[idx] = '\0';
        if (*p == '/') p++;

        if (idx == 0 || kstrcmp(part, ".")) {
            continue;
        } else if (kstrcmp(part, "..")) {
            if (cur->parent) cur = cur->parent;
        } else {
            cur = find_child(cur, part);
            if (!cur) return nullptr;
        }
    }
    return cur;
}

vfs_node_t* vfs_mkdir(const char* path) {
    if (!path || !root_node) return nullptr;
    const char* last_slash = nullptr;
    for (const char* s = path; *s; s++) {
        if (*s == '/') last_slash = s;
    }

    vfs_node_t* parent = nullptr;
    const char* dir_name = path;

    if (last_slash) {
        char parent_path[128];
        int len = last_slash - path;
        if (len == 0) {
            parent_path[0] = '/'; parent_path[1] = '\0';
        } else {
            for (int i = 0; i < len; i++) parent_path[i] = path[i];
            parent_path[len] = '\0';
        }
        parent = vfs_lookup(parent_path);
        dir_name = last_slash + 1;
    } else {
        parent = vfs_get_current_dir();
    }

    if (!parent || parent->type != FS_DIRECTORY) return nullptr;
    vfs_node_t* existing = find_child(parent, dir_name);
    if (existing) return existing;

    vfs_node_t* n = create_node(dir_name, FS_DIRECTORY, parent);
    vfs_refresh_gui_list();
    return n;
}

vfs_node_t* vfs_create_file(const char* path) {
    if (!path || !root_node) return nullptr;
    const char* last_slash = nullptr;
    for (const char* s = path; *s; s++) {
        if (*s == '/') last_slash = s;
    }

    vfs_node_t* parent = nullptr;
    const char* file_name = path;

    if (last_slash) {
        char parent_path[128];
        int len = last_slash - path;
        if (len == 0) {
            parent_path[0] = '/'; parent_path[1] = '\0';
        } else {
            for (int i = 0; i < len; i++) parent_path[i] = path[i];
            parent_path[len] = '\0';
        }
        parent = vfs_lookup(parent_path);
        file_name = last_slash + 1;
    } else {
        parent = vfs_get_current_dir();
    }

    if (!parent || parent->type != FS_DIRECTORY) return nullptr;
    vfs_node_t* existing = find_child(parent, file_name);
    if (existing && existing->type == FS_FILE) return existing;

    vfs_node_t* n = create_node(file_name, FS_FILE, parent);
    vfs_refresh_gui_list();
    return n;
}

bool vfs_write_file(const char* path, const char* data, uint32_t size) {
    vfs_node_t* node = vfs_lookup(path);
    if (!node) node = vfs_create_file(path);
    if (!node || node->type != FS_FILE) return false;

    if (node->content) kfree(node->content);
    node->content = static_cast<char*>(kmalloc(size + 1));
    if (!node->content) return false;

    memcpy(node->content, data, size);
    node->content[size] = '\0';
    node->size = size;

    vfs_refresh_gui_list();
    vfs_sync_to_disk();
    return true;
}

bool vfs_delete_node(vfs_node_t* node) {
    if (!node || node == root_node) return false;
    vfs_node_t* parent = node->parent;
    if (!parent) return false;

    // Unlink from parent / siblings
    if (parent->first_child == node) {
        parent->first_child = node->next_sibling;
    } else {
        vfs_node_t* prev = parent->first_child;
        while (prev && prev->next_sibling != node) {
            prev = prev->next_sibling;
        }
        if (prev) prev->next_sibling = node->next_sibling;
    }

    if (node->content) kfree(node->content);
    kfree(node);

    vfs_refresh_gui_list();
    vfs_sync_to_disk();
    return true;
}

void vfs_refresh_gui_list() {
    vfs_file_count = 0;
    vfs_node_t* cur = vfs_get_current_dir();
    if (!cur) return;

    // If not root, add ".." entry
    if (cur != root_node && vfs_file_count < MAX_GUI_ITEMS) {
        VFSGuiItem& item = vfs_gui_list[vfs_file_count++];
        kstrcpy(item.name, "..");
        item.is_dir = true;
        item.size = 0;
        item.node_ptr = cur->parent;
    }

    // List subdirectories first
    vfs_node_t* child = cur->first_child;
    while (child && vfs_file_count < MAX_GUI_ITEMS) {
        if (child->type == FS_DIRECTORY) {
            VFSGuiItem& item = vfs_gui_list[vfs_file_count++];
            kstrcpy(item.name, child->name);
            item.is_dir = true;
            item.size = 0;
            item.node_ptr = child;
        }
        child = child->next_sibling;
    }

    // List regular files
    child = cur->first_child;
    while (child && vfs_file_count < MAX_GUI_ITEMS) {
        if (child->type == FS_FILE) {
            VFSGuiItem& item = vfs_gui_list[vfs_file_count++];
            kstrcpy(item.name, child->name);
            item.is_dir = false;
            item.size = child->size;
            item.node_ptr = child;
        }
        child = child->next_sibling;
    }
}

// Flat file persistence to ATA IDE disk
void vfs_sync_to_disk() {
    if (!ata_present || !root_node) return;

    uint8_t sector[512] = {0};
    sector[0] = 'F'; sector[1] = 'S'; sector[2] = '0'; sector[3] = '2'; // FS02 = hierarchical format
    ata_write_sector(100, sector);
}

void vfs_init(uint32_t mod_start, uint32_t mod_end) {
    root_node = create_node("", FS_DIRECTORY, nullptr);
    current_dir = root_node;

    // Check ATA disk presence
    uint8_t st = inb(0x1F7);
    if (st != 0xFF && st != 0x00) {
        ata_present = true;
    }

    // Parse Multiboot TAR module
    if (mod_start != 0 && mod_end > mod_start) {
        uint32_t ptr = mod_start;
        while (ptr < mod_end) {
            auto* hdr = reinterpret_cast<tar_header*>(ptr);
            if (hdr->filename[0] == '\0') break;

            uint32_t sz = oct2bin(hdr->size, 11);
            char clean_name[128];
            const char* src_name = hdr->filename;
            if (src_name[0] == '.' && src_name[1] == '/') src_name += 2;

            int len = 0;
            while (src_name[len] && len < 127) {
                clean_name[len] = src_name[len];
                len++;
            }
            clean_name[len] = '\0';

            // Directory entry in TAR
            if (hdr->typeflag[0] == '5' || (len > 0 && clean_name[len - 1] == '/')) {
                if (len > 0 && clean_name[len - 1] == '/') clean_name[len - 1] = '\0';
                if (clean_name[0] != '\0') {
                    vfs_mkdir(clean_name);
                }
            }
            // Regular file entry
            else if (hdr->typeflag[0] == '0' || hdr->typeflag[0] == '\0') {
                const char* data = reinterpret_cast<const char*>(ptr + 512);
                vfs_write_file(clean_name, data, sz);
            }

            ptr += 512 + ((sz + 511) / 512) * 512;
        }
    }

    // Default structure if empty
    if (!root_node->first_child) {
        vfs_mkdir("/docs");
        vfs_mkdir("/system");
        vfs_write_file("/readme.txt", "Welcome to FishOS v2.0!\nNow with IDT, PIT, PMM, Heap and Hierarchical VFS.", 76);
        vfs_write_file("/docs/todo.txt", "1. Enjoy multitasking\n2. Run user mode programs\n3. Browse folders", 66);
    }

    vfs_refresh_gui_list();
}