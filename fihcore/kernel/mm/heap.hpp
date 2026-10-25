// Kernel heap: first-fit allocator with boundary tags over a fixed arena.
#pragma once
#include <stddef.h>
#include <stdint.h>

void heap_init(uint32_t size);
void* kmalloc(size_t n);  // 16-byte aligned, nullptr on failure
void* kzalloc(size_t n);
void kfree(void* p);
uint32_t heap_used();
uint32_t heap_total();
