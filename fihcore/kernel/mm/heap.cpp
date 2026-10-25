#include "heap.hpp"
#include "pmm.hpp"
#include "lib/lib.hpp"
#include "arch/x86/cpu.hpp"

// ponytail: single fixed arena with linear first-fit search; switch to size
// classes or a growable arena if allocation-heavy workloads show up.
namespace {
constexpr uint32_t MAGIC_USED = 0xA110CA7E, MAGIC_FREE = 0xF7EEB10C;
struct Header {
    uint32_t magic;
    uint32_t size; // whole block including header and footer
    uint32_t pad[2];
};
struct Footer {
    uint32_t size;
    uint32_t magic;
};
constexpr uint32_t OVERHEAD = sizeof(Header) + sizeof(Footer);
constexpr uint32_t MIN_BLOCK = OVERHEAD + 16;

uint8_t* arena;
uint32_t arena_size, used_bytes;

Footer* footer(Header* h) { return reinterpret_cast<Footer*>(reinterpret_cast<uint8_t*>(h) + h->size - sizeof(Footer)); }
void mark(Header* h, uint32_t size, uint32_t magic) {
    h->magic = magic;
    h->size = size;
    Footer* f = footer(h);
    f->size = size;
    f->magic = magic;
}
void check(Header* h) {
    if ((h->magic != MAGIC_USED && h->magic != MAGIC_FREE) || h->size < MIN_BLOCK ||
        reinterpret_cast<uint8_t*>(h) + h->size > arena + arena_size || footer(h)->size != h->size)
        panic("heap corruption at %p (magic %x size %u)", (uint32_t)h, h->magic, h->size);
}
}

void heap_init(uint32_t size) {
    size = align_up(size, PAGE_SIZE);
    uint32_t pa = pmm_alloc_contig(size / PAGE_SIZE);
    if (!pa) panic("heap: cannot reserve %u KiB", size / 1024);
    arena = reinterpret_cast<uint8_t*>(pa);
    arena_size = size;
    mark(reinterpret_cast<Header*>(arena), size, MAGIC_FREE);
    klog("heap: %u KiB at %p\n", size / 1024, pa);
}

void* kmalloc(size_t n) {
    if (n == 0 || n > arena_size) return nullptr;
    uint32_t need = align_up((uint32_t)n, 16) + OVERHEAD;
    uint32_t f = irq_save();
    for (uint8_t* p = arena; p < arena + arena_size;) {
        auto* h = reinterpret_cast<Header*>(p);
        check(h);
        if (h->magic == MAGIC_FREE && h->size >= need) {
            uint32_t rest = h->size - need;
            if (rest >= MIN_BLOCK) {
                mark(h, need, MAGIC_USED);
                mark(reinterpret_cast<Header*>(p + need), rest, MAGIC_FREE);
            } else {
                mark(h, h->size, MAGIC_USED);
            }
            used_bytes += h->size;
            irq_restore(f);
            return h + 1;
        }
        p += h->size;
    }
    irq_restore(f);
    return nullptr;
}

void* kzalloc(size_t n) {
    void* p = kmalloc(n);
    if (p) memset(p, 0, n);
    return p;
}

void kfree(void* ptr) {
    if (!ptr) return;
    uint32_t f = irq_save();
    auto* h = reinterpret_cast<Header*>(ptr) - 1;
    if (reinterpret_cast<uint8_t*>(h) < arena || reinterpret_cast<uint8_t*>(h) >= arena + arena_size)
        panic("kfree: pointer %p outside heap", (uint32_t)ptr);
    check(h);
    if (h->magic != MAGIC_USED) panic("kfree: double free at %p", (uint32_t)ptr);
    used_bytes -= h->size;
    uint32_t size = h->size;
    auto* next = reinterpret_cast<Header*>(reinterpret_cast<uint8_t*>(h) + size);
    if (reinterpret_cast<uint8_t*>(next) < arena + arena_size && next->magic == MAGIC_FREE) {
        check(next);
        size += next->size;
    }
    if (reinterpret_cast<uint8_t*>(h) > arena) {
        auto* pf = reinterpret_cast<Footer*>(reinterpret_cast<uint8_t*>(h) - sizeof(Footer));
        if (pf->magic == MAGIC_FREE) {
            auto* prev = reinterpret_cast<Header*>(reinterpret_cast<uint8_t*>(h) - pf->size);
            check(prev);
            size += prev->size;
            h = prev;
        }
    }
    mark(h, size, MAGIC_FREE);
    irq_restore(f);
}

uint32_t heap_used() { return used_bytes; }
uint32_t heap_total() { return arena_size; }
