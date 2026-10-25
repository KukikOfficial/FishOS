// Physical frame allocator (4 KiB frames, bitmap over the first 1 GiB).
#pragma once
#include <stdint.h>

constexpr uint32_t PAGE_SIZE = 4096;
constexpr uint32_t PMM_LIMIT = 0x40000000; // frames above 1 GiB are not used

struct MultibootInfo;
void pmm_init(const MultibootInfo* mbi, uint32_t reserved_end);
void pmm_reserve(uint32_t start, uint32_t end);
uint32_t pmm_alloc();                 // returns 0 when out of memory
uint32_t pmm_alloc_contig(uint32_t n); // n contiguous frames, 0 on failure
void pmm_free(uint32_t pa);
uint32_t pmm_free_frames();
uint32_t pmm_total_frames();
uint32_t pmm_top(); // highest usable physical address
