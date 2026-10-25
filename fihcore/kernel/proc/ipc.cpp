// Mailboxes, the service name registry, shared memory and display ownership.
#include "proc.hpp"
#include "arch/x86/cpu.hpp"
#include "dev/dev.hpp"
#include "lib/lib.hpp"
#include "mm/heap.hpp"
#include "mm/pmm.hpp"
#include "mm/vmm.hpp"

namespace {
struct Name {
    char name[32];
    uint32_t pid;
};
constexpr int MAX_NAMES = 16;
Name names[MAX_NAMES];

struct Shm {
    int id;
    uint32_t owner;
    uint32_t pages;
    uint32_t* frames;
    uint32_t mapcount;
    bool destroyed;
    uint32_t grants[8];
};
constexpr int MAX_SHM = 128;
constexpr uint32_t SHM_MAX_SIZE = 32 * 1024 * 1024;
Shm shms[MAX_SHM];
int next_shm_id = 1;

uint32_t display_pid;

Shm* shm_find(int id) {
    for (auto& s : shms) if (s.id == id && id > 0) return &s;
    return nullptr;
}

void shm_maybe_free(Shm* s) {
    if (!s->destroyed || s->mapcount) return;
    for (uint32_t i = 0; i < s->pages; i++) pmm_free(s->frames[i]);
    kfree(s->frames);
    *s = {};
}

bool shm_allowed(const Shm* s, uint32_t pid) {
    if (s->owner == pid) return true;
    for (uint32_t g : s->grants) if (g == pid) return true;
    return false;
}

Mapping* find_map(Proc* p, int shm) {
    for (auto& m : p->maps) if (m.pages && m.shm == shm) return &m;
    return nullptr;
}

void unmap_pages(Proc* p, Mapping* m) {
    for (uint32_t i = 0; i < m->pages; i++) vmm_unmap(p->pd, m->va + i * PAGE_SIZE);
    *m = {};
}

void framebuffer_released() {
    display_pid = 0;
    console_init(); // the boot console takes the screen back
    klog("display: released, kernel console restored\n");
}
}

// ---- mailboxes -------------------------------------------------------------

int mbox_push(Proc* p, const fih_msg_t& m) {
    if (!p || !p->mbox || p->state == PState::Zombie || p->state == PState::Dead) return -FIH_ESRCH;
    uint32_t irq = irq_save();
    if (m.type == FIH_MSG_MOUSE && p->mb_count) { // coalesce motion with the newest event
        fih_msg_t& last = p->mbox[(p->mb_head + p->mb_count - 1) % MAILBOX_CAP];
        if (last.type == FIH_MSG_MOUSE && last.from == 0 && last.a[2] == m.a[2]) {
            last.a[0] += m.a[0];
            last.a[1] += m.a[1];
            last.a[3] += m.a[3];
            irq_restore(irq);
            return 0;
        }
    }
    if (p->mb_count == MAILBOX_CAP) {
        irq_restore(irq);
        return -FIH_EAGAIN;
    }
    p->mbox[(p->mb_head + p->mb_count) % MAILBOX_CAP] = m;
    p->mb_count++;
    irq_restore(irq);
    proc_wake(p->mbox);
    return 0;
}

int mbox_recv(fih_msg_t* out, int32_t timeout_ms) {
    Proc* p = proc_current();
    uint32_t irq = irq_save();
    while (p->mb_count == 0) {
        if (timeout_ms == 0) { irq_restore(irq); return 0; }
        if (proc_block(p->mbox, timeout_ms > 0 ? (uint32_t)timeout_ms : 0) < 0) {
            irq_restore(irq);
            return -FIH_EINTR;
        }
        if (p->timed_out && p->mb_count == 0) { irq_restore(irq); return 0; }
    }
    *out = p->mbox[p->mb_head];
    p->mb_head = (p->mb_head + 1) % MAILBOX_CAP;
    p->mb_count--;
    irq_restore(irq);
    return 1;
}

void input_post(const fih_msg_t& m) {
    if (!display_pid) return;
    fih_msg_t copy = m;
    copy.from = 0;
    mbox_push(proc_by_pid(display_pid), copy);
}

// ---- user mapping region ---------------------------------------------------

int map_region(Proc* p, uint32_t pages, uint32_t* va) {
    uint32_t want = pages * PAGE_SIZE;
    uint32_t cand = USER_MAP_BASE;
    for (bool moved = true; moved;) {
        moved = false;
        for (auto& m : p->maps) {
            if (!m.pages) continue;
            uint32_t ms = m.va, me = m.va + m.pages * PAGE_SIZE;
            if (cand < me && cand + want > ms) { cand = me; moved = true; }
        }
    }
    if (cand + want > USER_MAP_END || cand + want < cand) return -FIH_ENOMEM;
    for (auto& m : p->maps) {
        if (m.pages) continue;
        m.va = cand;
        m.pages = pages;
        *va = cand;
        return (int)(&m - p->maps);
    }
    return -FIH_ENOMEM;
}

// ---- shared memory ---------------------------------------------------------

int shm_create(uint32_t size) {
    if (size == 0 || size > SHM_MAX_SIZE) return -FIH_EINVAL;
    Shm* s = nullptr;
    for (auto& c : shms) if (!c.id) { s = &c; break; }
    if (!s) return -FIH_ENOMEM;
    uint32_t pages = align_up(size, PAGE_SIZE) / PAGE_SIZE;
    if (pages + 256 > pmm_free_frames()) return -FIH_ENOMEM;
    uint32_t* frames = (uint32_t*)kmalloc(pages * 4);
    if (!frames) return -FIH_ENOMEM;
    for (uint32_t i = 0; i < pages; i++) {
        frames[i] = pmm_alloc();
        if (!frames[i]) {
            while (i--) pmm_free(frames[i]);
            kfree(frames);
            return -FIH_ENOMEM;
        }
        memset(reinterpret_cast<void*>(frames[i]), 0, PAGE_SIZE);
    }
    *s = {};
    s->id = next_shm_id++;
    s->owner = proc_current()->pid;
    s->pages = pages;
    s->frames = frames;
    return s->id;
}

int shm_map(int id, uint32_t* va) {
    Proc* p = proc_current();
    Shm* s = shm_find(id);
    if (!s || s->destroyed) return -FIH_ENOENT;
    if (!shm_allowed(s, p->pid)) return -FIH_EPERM;
    if (Mapping* m = find_map(p, id)) { *va = m->va; return 0; }
    int slot = map_region(p, s->pages, va);
    if (slot < 0) return slot;
    p->maps[slot].shm = id;
    for (uint32_t i = 0; i < s->pages; i++) {
        if (vmm_map(p->pd, *va + i * PAGE_SIZE, s->frames[i], PTE_WRITE | PTE_USER | PTE_SHARED) < 0) {
            p->maps[slot].pages = i;
            unmap_pages(p, &p->maps[slot]);
            return -FIH_ENOMEM;
        }
    }
    s->mapcount++;
    return 0;
}

int shm_unmap(int id) {
    Proc* p = proc_current();
    Mapping* m = find_map(p, id);
    Shm* s = shm_find(id);
    if (!m || !s || id <= 0) return -FIH_ENOENT;
    unmap_pages(p, m);
    s->mapcount--;
    shm_maybe_free(s);
    return 0;
}

int shm_grant(int id, uint32_t pid) {
    Shm* s = shm_find(id);
    if (!s || s->destroyed) return -FIH_ENOENT;
    if (s->owner != proc_current()->pid) return -FIH_EPERM;
    for (uint32_t& g : s->grants) if (g == pid) return 0;
    for (uint32_t& g : s->grants) if (!g) { g = pid; return 0; }
    return -FIH_ENOMEM;
}

int shm_destroy(int id) {
    Shm* s = shm_find(id);
    if (!s || s->destroyed) return -FIH_ENOENT;
    if (s->owner != proc_current()->pid) return -FIH_EPERM;
    s->destroyed = true;
    shm_maybe_free(s);
    return 0;
}

int shm_size(int id) {
    Shm* s = shm_find(id);
    if (!s || s->destroyed) return -FIH_ENOENT;
    if (!shm_allowed(s, proc_current()->pid)) return -FIH_EPERM;
    return (int)(s->pages * PAGE_SIZE);
}

// ---- display ---------------------------------------------------------------

int display_acquire(fih_display_t* out) {
    Proc* p = proc_current();
    if (!g_fb.phys || g_fb.bpp != 32) return -FIH_ENODEV;
    if (display_pid && display_pid != p->pid && proc_by_pid(display_pid)) return -FIH_EBUSY;
    uint32_t size = g_fb.pitch * g_fb.height;
    uint32_t pages = align_up(size, PAGE_SIZE) / PAGE_SIZE, va;
    Mapping* existing = find_map(p, 0);
    if (existing) {
        va = existing->va;
    } else {
        int slot = map_region(p, pages, &va);
        if (slot < 0) return slot;
        p->maps[slot].shm = 0;
        for (uint32_t i = 0; i < pages; i++)
            if (vmm_map(p->pd, va + i * PAGE_SIZE, align_down(g_fb.phys, PAGE_SIZE) + i * PAGE_SIZE,
                        PTE_WRITE | PTE_USER | PTE_SHARED) < 0)
                return -FIH_ENOMEM;
    }
    display_pid = p->pid;
    console_disable();
    out->vaddr = va + (g_fb.phys & (PAGE_SIZE - 1));
    out->width = g_fb.width;
    out->height = g_fb.height;
    out->pitch = g_fb.pitch;
    out->bpp = g_fb.bpp;
    klog("display: acquired by pid %u (%ux%u)\n", p->pid, g_fb.width, g_fb.height);
    return 0;
}

// ---- names -----------------------------------------------------------------

int name_register(const char* name) {
    if (!name[0] || strlen(name) >= sizeof(names[0].name)) return -FIH_EINVAL;
    Name* free_slot = nullptr;
    for (auto& n : names) {
        if (n.pid && strcmp(n.name, name) == 0) {
            if (proc_by_pid(n.pid)) return -FIH_EEXIST;
            n.pid = 0;
        }
        if (!n.pid && !free_slot) free_slot = &n;
    }
    if (!free_slot) return -FIH_ENOMEM;
    strlcpy(free_slot->name, name, sizeof(free_slot->name));
    free_slot->pid = proc_current()->pid;
    return 0;
}

int name_lookup(const char* name) {
    for (auto& n : names)
        if (n.pid && strcmp(n.name, name) == 0 && proc_by_pid(n.pid)) return (int)n.pid;
    return -FIH_ENOENT;
}

void ipc_release(Proc* p) {
    for (auto& n : names) if (n.pid == p->pid) n.pid = 0;
    for (auto& m : p->maps) {
        if (!m.pages) continue;
        int id = m.shm;
        unmap_pages(p, &m);
        if (Shm* s = shm_find(id)) {
            s->mapcount--;
            shm_maybe_free(s);
        }
    }
    for (auto& s : shms) {
        if (!s.id) continue;
        if (s.owner == p->pid && !s.destroyed) {
            s.destroyed = true;
            shm_maybe_free(&s);
        }
        for (uint32_t& g : s.grants) if (g == p->pid) g = 0;
    }
    if (display_pid == p->pid) framebuffer_released();
}
