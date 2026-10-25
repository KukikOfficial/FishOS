// User-space allocator: first-fit free list over memory obtained with brk.
// ponytail: linear free-list search; add size classes if apps get alloc-heavy.
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fihcore/syscall.h>

typedef struct Block {
    size_t size;        // payload bytes
    uint32_t magic;
    uint32_t is_free;
    struct Block* next; // address-ordered list of all blocks
    uint32_t pad[3];
} Block;

#define MAGIC 0xFAB1E5u
static Block* head;
static uint32_t heap_end;

static Block* grow(size_t need) {
    if (!heap_end) heap_end = (uint32_t)fih_brk(0);
    size_t bytes = (need + sizeof(Block) + 0xFFFF) & ~0xFFFFu;
    uint32_t start = heap_end;
    if (fih_brk(start + bytes) != (int)(start + bytes)) return 0;
    heap_end = start + bytes;
    Block* b = (Block*)start;
    b->size = bytes - sizeof(Block);
    b->magic = MAGIC;
    b->is_free = 1;
    b->next = 0;
    Block** link = &head;
    Block* last = 0;
    while (*link) { last = *link; link = &(*link)->next; }
    if (last && last->is_free && (uint8_t*)(last + 1) + last->size == (uint8_t*)b) {
        last->size += bytes;
        return last;
    }
    *link = b;
    return b;
}

void* malloc(size_t n) {
    if (n == 0) n = 1;
    if (n > 0x20000000) return 0;
    n = (n + 15) & ~(size_t)15;
    Block* b = head;
    for (; b; b = b->next)
        if (b->is_free && b->size >= n) break;
    if (!b && !(b = grow(n))) return 0;
    if (b->size >= n + sizeof(Block) + 32) {
        Block* rest = (Block*)((uint8_t*)(b + 1) + n);
        rest->size = b->size - n - sizeof(Block);
        rest->magic = MAGIC;
        rest->is_free = 1;
        rest->next = b->next;
        b->next = rest;
        b->size = n;
    }
    b->is_free = 0;
    return b + 1;
}

void free(void* p) {
    if (!p) return;
    Block* b = (Block*)p - 1;
    if (b->magic != MAGIC || b->is_free) {
        static const char msg[] = "malloc: invalid free\n";
        fih_log(msg, sizeof(msg) - 1);
        return;
    }
    b->is_free = 1;
    // Merge with the following block, then let a free predecessor absorb us.
    if (b->next && b->next->is_free && (uint8_t*)(b + 1) + b->size == (uint8_t*)b->next) {
        b->size += sizeof(Block) + b->next->size;
        b->next = b->next->next;
    }
    for (Block* c = head; c && c != b; c = c->next) {
        if (c->next == b && c->is_free && (uint8_t*)(c + 1) + c->size == (uint8_t*)b) {
            c->size += sizeof(Block) + b->size;
            c->next = b->next;
            break;
        }
    }
}

void* calloc(size_t n, size_t size) {
    if (size && n > (size_t)-1 / size) return 0;
    void* p = malloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}

void* realloc(void* p, size_t n) {
    if (!p) return malloc(n);
    Block* b = (Block*)p - 1;
    if (b->size >= n) return p;
    void* q = malloc(n);
    if (!q) return 0;
    memcpy(q, p, b->size);
    free(p);
    return q;
}

void exit(int code) { fih_exit(code); }
