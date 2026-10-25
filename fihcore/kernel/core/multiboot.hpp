#pragma once
#include <stdint.h>

constexpr uint32_t MULTIBOOT_MAGIC = 0x2BADB002;

struct [[gnu::packed]] MultibootInfo {
    uint32_t flags;
    uint32_t mem_lower, mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count, mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length, mmap_addr;
    uint32_t drives_length, drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info, vbe_mode_info;
    uint16_t vbe_mode, vbe_interface_seg, vbe_interface_off, vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch, framebuffer_width, framebuffer_height;
    uint8_t framebuffer_bpp, framebuffer_type;
};

struct [[gnu::packed]] MultibootModule {
    uint32_t start, end, string, reserved;
};

struct [[gnu::packed]] MultibootMmap {
    uint32_t size;
    uint64_t addr, len;
    uint32_t type;
};

enum : uint32_t {
    MB_INFO_MEMORY = 1 << 0,
    MB_INFO_CMDLINE = 1 << 2,
    MB_INFO_MODS = 1 << 3,
    MB_INFO_MMAP = 1 << 6,
    MB_INFO_FRAMEBUFFER = 1 << 12,
};
