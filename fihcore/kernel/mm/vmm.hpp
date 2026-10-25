// Paging. The kernel identity-maps physical memory below 1 GiB (supervisor
// only) and keeps MMIO above 0xE0000000. Every process gets its own page
// directory that shares those kernel entries; user space lives in between.
#pragma once
#include <stdint.h>

constexpr uint32_t USER_BASE = 0x40000000;
constexpr uint32_t USER_IMAGE_END = 0x70000000;  // ELF segments and heap
constexpr uint32_t USER_MAP_BASE = 0x80000000;   // shared memory and framebuffer
constexpr uint32_t USER_MAP_END = 0xB0000000;
constexpr uint32_t USER_STACK_TOP = 0xBFFFF000;
constexpr uint32_t USER_STACK_SIZE = 0x00400000; // grows on demand
constexpr uint32_t USER_END = 0xC0000000;
constexpr uint32_t KERNEL_MMIO_BASE = 0xE0000000;

enum : uint32_t {
    PTE_PRESENT = 1, PTE_WRITE = 2, PTE_USER = 4, PTE_LARGE = 0x80,
    PTE_SHARED = 0x200, // frame not owned by this mapping: never freed on unmap
};

void vmm_init();
uint32_t vmm_kernel_pd();
uint32_t vmm_map_mmio(uint32_t phys, uint32_t size); // returns kernel virtual address

uint32_t vmm_create_space();
void vmm_destroy_space(uint32_t pd);
int vmm_map(uint32_t pd, uint32_t va, uint32_t pa, uint32_t flags);
uint32_t vmm_pte(uint32_t pd, uint32_t va); // 0 when not mapped
void vmm_unmap(uint32_t pd, uint32_t va);   // frees owned frames
int vmm_alloc_range(uint32_t pd, uint32_t va, uint32_t size); // zeroed, user, writable

// Validates that [va, va+len) is user memory mapped in the current address
// space (demand-faulting stack pages as needed).
bool vmm_user_ok(uint32_t va, uint32_t len, bool write);
bool vmm_handle_user_fault(uint32_t addr, uint32_t err);
