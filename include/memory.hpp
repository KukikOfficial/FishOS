#pragma once
#include <stdint.h>
#include <stddef.h>
#include "multiboot.hpp"

constexpr uint32_t PAGE_SIZE = 4096;

// Physical Memory Manager (PMM)
void pmm_init(multiboot_info* mbi, uint32_t kernel_end_addr);
void* pmm_alloc_page();
void pmm_free_page(void* addr);
uint32_t pmm_get_total_memory();
uint32_t pmm_get_used_memory();
uint32_t pmm_get_free_memory();

// Kernel Heap Allocator
void kheap_init(uint32_t start_addr, uint32_t size_bytes);
void* kmalloc(size_t size);
void kfree(void* ptr);
void* krealloc(void* ptr, size_t size);
void* kcalloc(size_t num, size_t size);

// C++ operator new/delete
void* operator new(size_t size);
void* operator new[](size_t size);
void operator delete(void* ptr) noexcept;
void operator delete[](void* ptr) noexcept;
void operator delete(void* ptr, size_t) noexcept;
void operator delete[](void* ptr, size_t) noexcept;
