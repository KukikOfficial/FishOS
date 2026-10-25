#include "vmm.hpp"
#include "pmm.hpp"
#include "lib/lib.hpp"
#include "arch/x86/cpu.hpp"
#include "proc/proc.hpp"

namespace {
alignas(4096) uint32_t kernel_pd[1024];
alignas(4096) uint32_t low_pt[1024];
uint32_t mmio_next = KERNEL_MMIO_BASE;

inline uint32_t* table(uint32_t pa) { return reinterpret_cast<uint32_t*>(pa); }
inline bool is_user_va(uint32_t va) { return va >= USER_BASE && va < USER_END; }

uint32_t* pte_ptr(uint32_t pd, uint32_t va, bool create, bool user) {
    uint32_t* dir = table(pd);
    uint32_t& pde = dir[va >> 22];
    if (!(pde & PTE_PRESENT)) {
        if (!create) return nullptr;
        uint32_t pt = pmm_alloc();
        if (!pt) return nullptr;
        memset(table(pt), 0, PAGE_SIZE);
        pde = pt | PTE_PRESENT | PTE_WRITE | (user ? (uint32_t)PTE_USER : 0u);
    }
    if (pde & PTE_LARGE) return nullptr;
    return &table(pde & ~0xFFFu)[(va >> 12) & 1023];
}

void flush(uint32_t pd, uint32_t va) {
    if (read_cr3() == pd) invlpg(va);
}
}

void vmm_init() {
    // 0-4 MiB through a page table so that page 0 stays unmapped (null traps).
    for (uint32_t i = 1; i < 1024; i++) low_pt[i] = (i * PAGE_SIZE) | PTE_PRESENT | PTE_WRITE;
    kernel_pd[0] = (uint32_t)low_pt | PTE_PRESENT | PTE_WRITE;
    uint32_t top = pmm_top();
    for (uint32_t a = 0x400000; a < top && a < PMM_LIMIT; a += 0x400000)
        kernel_pd[a >> 22] = a | PTE_PRESENT | PTE_WRITE | PTE_LARGE;
    uint32_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    asm volatile("mov %0, %%cr4" : : "r"(cr4 | 0x10)); // PSE
    write_cr3((uint32_t)kernel_pd);
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    asm volatile("mov %0, %%cr0" : : "r"(cr0 | 0x80010000)); // PG | WP
    klog("vmm: paging enabled, kernel maps %u MiB\n", kmin(top, PMM_LIMIT) >> 20);
}

uint32_t vmm_kernel_pd() { return (uint32_t)kernel_pd; }

uint32_t vmm_map_mmio(uint32_t phys, uint32_t size) {
    uint32_t base = align_down(phys, PAGE_SIZE);
    uint32_t len = align_up(phys + size, PAGE_SIZE) - base;
    uint32_t va = mmio_next;
    for (uint32_t off = 0; off < len; off += PAGE_SIZE) {
        if (vmm_map((uint32_t)kernel_pd, va + off, base + off, PTE_PRESENT | PTE_WRITE) < 0)
            panic("vmm: cannot map MMIO %p", phys);
    }
    mmio_next = align_up(va + len, 0x400000);
    return va + (phys - base);
}

uint32_t vmm_create_space() {
    uint32_t pd = pmm_alloc();
    if (!pd) return 0;
    uint32_t* dir = table(pd);
    memset(dir, 0, PAGE_SIZE);
    for (uint32_t i = 0; i < (USER_BASE >> 22); i++) dir[i] = kernel_pd[i];
    for (uint32_t i = USER_END >> 22; i < 1024; i++) dir[i] = kernel_pd[i];
    return pd;
}

void vmm_destroy_space(uint32_t pd) {
    KASSERT(pd != (uint32_t)kernel_pd && read_cr3() != pd);
    uint32_t* dir = table(pd);
    for (uint32_t i = USER_BASE >> 22; i < (USER_END >> 22); i++) {
        if (!(dir[i] & PTE_PRESENT)) continue;
        uint32_t* pt = table(dir[i] & ~0xFFFu);
        for (int j = 0; j < 1024; j++)
            if ((pt[j] & PTE_PRESENT) && !(pt[j] & PTE_SHARED)) pmm_free(pt[j] & ~0xFFFu);
        pmm_free(dir[i] & ~0xFFFu);
    }
    pmm_free(pd);
}

int vmm_map(uint32_t pd, uint32_t va, uint32_t pa, uint32_t flags) {
    uint32_t* pte = pte_ptr(pd, va, true, is_user_va(va));
    if (!pte) return -FIH_ENOMEM;
    *pte = (pa & ~0xFFFu) | (flags & 0xFFF) | PTE_PRESENT;
    flush(pd, va);
    return 0;
}

uint32_t vmm_pte(uint32_t pd, uint32_t va) {
    uint32_t* pte = pte_ptr(pd, va, false, false);
    return pte ? *pte : 0;
}

void vmm_unmap(uint32_t pd, uint32_t va) {
    uint32_t* pte = pte_ptr(pd, va, false, false);
    if (!pte || !(*pte & PTE_PRESENT)) return;
    if (!(*pte & PTE_SHARED)) pmm_free(*pte & ~0xFFFu);
    *pte = 0;
    flush(pd, va);
}

int vmm_alloc_range(uint32_t pd, uint32_t va, uint32_t size) {
    uint32_t start = align_down(va, PAGE_SIZE), end = align_up(va + size, PAGE_SIZE);
    for (uint32_t a = start; a < end; a += PAGE_SIZE) {
        if (vmm_pte(pd, a) & PTE_PRESENT) continue;
        uint32_t f = pmm_alloc();
        if (!f) return -FIH_ENOMEM;
        memset(table(f), 0, PAGE_SIZE);
        if (vmm_map(pd, a, f, PTE_WRITE | PTE_USER) < 0) {
            pmm_free(f);
            return -FIH_ENOMEM;
        }
    }
    return 0;
}

bool vmm_handle_user_fault(uint32_t addr, uint32_t err) {
    if (err & 1) return false; // protection violation, not a missing page
    if (addr < USER_STACK_TOP - USER_STACK_SIZE || addr >= USER_STACK_TOP) return false;
    Proc* p = proc_current();
    if (!p || !p->pd) return false;
    if (vmm_alloc_range(p->pd, addr, 1) < 0) return false;
    p->pages++;
    return true;
}

bool vmm_user_ok(uint32_t va, uint32_t len, bool write) {
    if (len == 0) return true;
    if (va < USER_BASE || va >= USER_END || len > USER_END - va) return false;
    uint32_t pd = read_cr3();
    for (uint32_t a = align_down(va, PAGE_SIZE); a < va + len; a += PAGE_SIZE) {
        uint32_t pte = vmm_pte(pd, a);
        if (!(pte & PTE_PRESENT)) {
            if (!vmm_handle_user_fault(a, 0)) return false;
            pte = vmm_pte(pd, a);
        }
        if (!(pte & PTE_USER) || (write && !(pte & PTE_WRITE))) return false;
        if (a + PAGE_SIZE < a) break;
    }
    return true;
}
