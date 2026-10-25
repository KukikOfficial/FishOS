#include "pmm.hpp"
#include "core/multiboot.hpp"
#include "lib/lib.hpp"

namespace {
constexpr uint32_t FRAMES = PMM_LIMIT / PAGE_SIZE;
uint32_t bitmap[FRAMES / 32]; // 1 = used
uint32_t total, free_count, hint, top;

bool used(uint32_t f) { return bitmap[f / 32] & (1u << (f % 32)); }
void set_used(uint32_t f) {
    if (!used(f)) { bitmap[f / 32] |= 1u << (f % 32); free_count--; }
}
void set_free(uint32_t f) {
    if (used(f)) { bitmap[f / 32] &= ~(1u << (f % 32)); free_count++; }
}
}

void pmm_init(const MultibootInfo* mbi, uint32_t reserved_end) {
    memset(bitmap, 0xFF, sizeof(bitmap));
    free_count = 0;
    if (!(mbi->flags & MB_INFO_MMAP)) panic("bootloader provided no memory map");
    uint32_t p = mbi->mmap_addr, end = mbi->mmap_addr + mbi->mmap_length;
    while (p < end) {
        auto* e = reinterpret_cast<const MultibootMmap*>(p);
        if (e->type == 1 && e->addr < PMM_LIMIT) {
            uint64_t last = e->addr + e->len;
            if (last > PMM_LIMIT) last = PMM_LIMIT;
            uint32_t s = align_up((uint32_t)e->addr, PAGE_SIZE);
            uint32_t t = (uint32_t)last & ~(PAGE_SIZE - 1);
            for (uint32_t a = s; a < t; a += PAGE_SIZE) {
                set_free(a / PAGE_SIZE);
                total++;
            }
            if (t > top) top = t;
        }
        p += e->size + 4;
    }
    pmm_reserve(0, 0x100000); // BIOS area, VGA memory, boot structures
    pmm_reserve(0x100000, reserved_end);
    klog("pmm: %u MiB usable, %u MiB free\n", total / 256, free_count / 256);
}

void pmm_reserve(uint32_t start, uint32_t end) {
    for (uint32_t a = align_down(start, PAGE_SIZE); a < end && a < PMM_LIMIT; a += PAGE_SIZE)
        set_used(a / PAGE_SIZE);
}

uint32_t pmm_alloc() {
    for (uint32_t i = 0; i < FRAMES / 32; i++) {
        uint32_t w = (hint + i) % (FRAMES / 32);
        if (bitmap[w] == 0xFFFFFFFF) continue;
        for (uint32_t b = 0; b < 32; b++) {
            if (!(bitmap[w] & (1u << b))) {
                uint32_t f = w * 32 + b;
                set_used(f);
                hint = w;
                return f * PAGE_SIZE;
            }
        }
    }
    return 0;
}

uint32_t pmm_alloc_contig(uint32_t n) {
    uint32_t run = 0;
    for (uint32_t f = 0x100000 / PAGE_SIZE; f < FRAMES; f++) {
        run = used(f) ? 0 : run + 1;
        if (run == n) {
            uint32_t first = f + 1 - n;
            for (uint32_t i = first; i <= f; i++) set_used(i);
            return first * PAGE_SIZE;
        }
    }
    return 0;
}

void pmm_free(uint32_t pa) {
    KASSERT((pa & (PAGE_SIZE - 1)) == 0 && pa >= 0x100000 && pa < PMM_LIMIT);
    if (!used(pa / PAGE_SIZE)) panic("pmm: double free of frame %p", pa);
    set_free(pa / PAGE_SIZE);
}

uint32_t pmm_free_frames() { return free_count; }
uint32_t pmm_total_frames() { return total; }
uint32_t pmm_top() { return top; }
