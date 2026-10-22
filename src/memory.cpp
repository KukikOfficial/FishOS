#include "memory.hpp"
#include "string.hpp"

// Physical memory bitmap: support up to 512 MB RAM (131072 pages of 4KB)
#define MAX_PAGES 131072
static uint32_t page_bitmap[MAX_PAGES / 32];
static uint32_t total_pages = 0;
static uint32_t used_pages = 0;

static inline void bitmap_set(uint32_t page) {
    if (page < MAX_PAGES) {
        if (!(page_bitmap[page / 32] & (1 << (page % 32)))) {
            page_bitmap[page / 32] |= (1 << (page % 32));
            used_pages++;
        }
    }
}

static inline void bitmap_clear(uint32_t page) {
    if (page < MAX_PAGES) {
        if (page_bitmap[page / 32] & (1 << (page % 32))) {
            page_bitmap[page / 32] &= ~(1 << (page % 32));
            used_pages--;
        }
    }
}

static inline bool bitmap_test(uint32_t page) {
    if (page >= MAX_PAGES) return true;
    return (page_bitmap[page / 32] & (1 << (page % 32))) != 0;
}

void pmm_init(multiboot_info* mbi, uint32_t kernel_end_addr) {
    // Default: mark everything as used
    memset(page_bitmap, 0xFF, sizeof(page_bitmap));

    uint32_t mem_kb = 64 * 1024; // default 64 MB
    if (mbi->flags & (1 << 0)) {
        mem_kb = mbi->mem_upper + 1024;
    }
    total_pages = (mem_kb * 1024) / PAGE_SIZE;
    if (total_pages > MAX_PAGES) total_pages = MAX_PAGES;
    used_pages = total_pages;

    // Free all pages from kernel_end_addr up to total_pages
    uint32_t first_free_page = (kernel_end_addr + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t p = first_free_page; p < total_pages; p++) {
        bitmap_clear(p);
    }
}

void* pmm_alloc_page() {
    for (uint32_t i = 0; i < total_pages / 32; i++) {
        if (page_bitmap[i] != 0xFFFFFFFF) {
            for (uint32_t b = 0; b < 32; b++) {
                if (!(page_bitmap[i] & (1 << b))) {
                    uint32_t page = i * 32 + b;
                    bitmap_set(page);
                    return reinterpret_cast<void*>(page * PAGE_SIZE);
                }
            }
        }
    }
    return nullptr; // Out of memory
}

void pmm_free_page(void* addr) {
    uint32_t page = reinterpret_cast<uint32_t>(addr) / PAGE_SIZE;
    bitmap_clear(page);
}

uint32_t pmm_get_total_memory() {
    return total_pages * PAGE_SIZE;
}

uint32_t pmm_get_used_memory() {
    return used_pages * PAGE_SIZE;
}

uint32_t pmm_get_free_memory() {
    return (total_pages - used_pages) * PAGE_SIZE;
}

// -------------------------------------------------------------
// Kernel Heap Allocator
// -------------------------------------------------------------

struct heap_chunk {
    uint32_t size;       // Payload size in bytes
    bool is_free;
    heap_chunk* next;
    heap_chunk* prev;
};

static heap_chunk* heap_head = nullptr;

void kheap_init(uint32_t start_addr, uint32_t size_bytes) {
    // Align start_addr to 16 bytes
    uint32_t aligned_start = (start_addr + 15) & ~15;
    uint32_t usable_size = size_bytes - (aligned_start - start_addr);

    heap_head = reinterpret_cast<heap_chunk*>(aligned_start);
    heap_head->size = usable_size - sizeof(heap_chunk);
    heap_head->is_free = true;
    heap_head->next = nullptr;
    heap_head->prev = nullptr;
}

void* kmalloc(size_t size) {
    if (size == 0) return nullptr;

    // Align request to 16 bytes
    size = (size + 15) & ~15;

    heap_chunk* cur = heap_head;
    while (cur) {
        if (cur->is_free && cur->size >= size) {
            // Can we split?
            if (cur->size >= size + sizeof(heap_chunk) + 16) {
                uint32_t next_addr = reinterpret_cast<uint32_t>(cur) + sizeof(heap_chunk) + size;
                heap_chunk* split = reinterpret_cast<heap_chunk*>(next_addr);
                split->size = cur->size - size - sizeof(heap_chunk);
                split->is_free = true;
                split->next = cur->next;
                split->prev = cur;

                if (cur->next) cur->next->prev = split;
                cur->next = split;
                cur->size = size;
            }
            cur->is_free = false;
            return reinterpret_cast<void*>(reinterpret_cast<uint32_t>(cur) + sizeof(heap_chunk));
        }
        cur = cur->next;
    }

    return nullptr; // Out of heap memory
}

void kfree(void* ptr) {
    if (!ptr) return;

    heap_chunk* chunk = reinterpret_cast<heap_chunk*>(reinterpret_cast<uint32_t>(ptr) - sizeof(heap_chunk));
    chunk->is_free = true;

    // Merge with next chunk if free
    if (chunk->next && chunk->next->is_free) {
        chunk->size += sizeof(heap_chunk) + chunk->next->size;
        chunk->next = chunk->next->next;
        if (chunk->next) chunk->next->prev = chunk;
    }

    // Merge with previous chunk if free
    if (chunk->prev && chunk->prev->is_free) {
        chunk->prev->size += sizeof(heap_chunk) + chunk->size;
        chunk->prev->next = chunk->next;
        if (chunk->next) chunk->next->prev = chunk->prev;
    }
}

void* kcalloc(size_t num, size_t size) {
    size_t total = num * size;
    void* ptr = kmalloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) { kfree(ptr); return nullptr; }

    heap_chunk* chunk = reinterpret_cast<heap_chunk*>(reinterpret_cast<uint32_t>(ptr) - sizeof(heap_chunk));
    if (chunk->size >= size) return ptr;

    void* new_ptr = kmalloc(size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, chunk->size);
        kfree(ptr);
    }
    return new_ptr;
}

// C++ operator new/delete
void* operator new(size_t size) { return kmalloc(size); }
void* operator new[](size_t size) { return kmalloc(size); }
void operator delete(void* ptr) noexcept { kfree(ptr); }
void operator delete[](void* ptr) noexcept { kfree(ptr); }
void operator delete(void* ptr, size_t) noexcept { kfree(ptr); }
void operator delete[](void* ptr, size_t) noexcept { kfree(ptr); }
