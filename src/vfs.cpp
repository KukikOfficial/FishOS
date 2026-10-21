#include "vfs.hpp"
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

VFSFile vfs[MAX_FILES];
int vfs_file_count = 0;

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

void vfs_add_file(const char* name, const char* data, uint32_t size) {
    if (vfs_file_count >= MAX_FILES) return;
    VFSFile& f = vfs[vfs_file_count++];
    f.used = true;
    f.size = (size < MAX_FILE_SIZE - 1) ? size : MAX_FILE_SIZE - 1;

    if (name[0] == '.' && name[1] == '/') name += 2;
    kstrcpy(f.name, name);

    for (uint32_t i = 0; i < f.size; i++) f.content[i] = data[i];
    f.content[f.size] = '\0';
}

void vfs_sync_to_disk() {
    if (!ata_present) return;
    uint8_t sector[512] = {0};
    sector[0] = 'F'; sector[1] = 'S'; sector[2] = '0'; sector[3] = '1';
    sector[4] = static_cast<uint8_t>(vfs_file_count);
    ata_write_sector(100, sector);

    uint8_t* raw = reinterpret_cast<uint8_t*>(vfs);
    uint32_t total_bytes = sizeof(VFSFile) * MAX_FILES;
    uint32_t num_sectors = (total_bytes + 511) / 512;
    for (uint32_t s = 0; s < num_sectors; s++) {
        ata_write_sector(101 + s, raw + s * 512);
    }
}

void vfs_delete_file(int index) {
    if (index < 0 || index >= vfs_file_count) return;
    for (int i = index; i < vfs_file_count - 1; i++) {
        vfs[i] = vfs[i + 1];
    }
    vfs_file_count--;
    vfs_sync_to_disk();
}

void vfs_save_file(const char* name, const char* data, uint32_t size) {
    for (int i = 0; i < vfs_file_count; i++) {
        if (kstrcmp(vfs[i].name, name)) {
            vfs[i].size = (size < MAX_FILE_SIZE - 1) ? size : MAX_FILE_SIZE - 1;
            for (uint32_t j = 0; j < vfs[i].size; j++) vfs[i].content[j] = data[j];
            vfs[i].content[vfs[i].size] = '\0';
            vfs_sync_to_disk();
            return;
        }
    }
    vfs_add_file(name, data, size);
    vfs_sync_to_disk();
}

void vfs_init(uint32_t mod_start, uint32_t mod_end) {
    vfs_file_count = 0;

    uint8_t st = inb(0x1F7);
    if (st != 0xFF && st != 0x00) {
        uint8_t check_sec[512];
        ata_read_sector(100, check_sec);
        if (check_sec[0] == 'F' && check_sec[1] == 'S' && check_sec[2] == '0' && check_sec[3] == '1') {
            ata_present = true;
            vfs_file_count = check_sec[4];
            uint8_t* raw = reinterpret_cast<uint8_t*>(vfs);
            uint32_t total_bytes = sizeof(VFSFile) * MAX_FILES;
            uint32_t num_sectors = (total_bytes + 511) / 512;
            for (uint32_t s = 0; s < num_sectors; s++) {
                ata_read_sector(101 + s, raw + s * 512);
            }
            return;
        } else {
            ata_present = true;
        }
    }

    if (mod_start != 0 && mod_end > mod_start) {
        uint32_t ptr = mod_start;
        while (ptr < mod_end) {
            auto* hdr = reinterpret_cast<tar_header*>(ptr);
            if (hdr->filename[0] == '\0') break;
            uint32_t sz = oct2bin(hdr->size, 11);
            if (hdr->typeflag[0] == '0' || hdr->typeflag[0] == '\0') {
                const char* data = reinterpret_cast<const char*>(ptr + 512);
                vfs_add_file(hdr->filename, data, sz);
            }
            ptr += 512 + ((sz + 511) / 512) * 512;
        }
    }

    if (vfs_file_count == 0) {
        vfs_add_file("readme.txt", "Welcome to Modular LiteOS!\nCode is now clean and split.", 56);
        vfs_add_file("todo.txt", "1. Add Calculator\n2. Add Snake game\n3. Enjoy modular OS", 56);
    }

    vfs_sync_to_disk();
}