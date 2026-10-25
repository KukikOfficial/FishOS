// Anonymous pipes with blocking semantics.
#include "vfs.hpp"
#include "lib/lib.hpp"
#include "mm/heap.hpp"
#include "proc/proc.hpp"
#include "arch/x86/cpu.hpp"

struct Pipe {
    uint8_t buf[4096];
    uint32_t head, count;
    uint32_t readers, writers;
};

int pipe_create(uint32_t flags, File** rd, File** wr) {
    Pipe* p = (Pipe*)kzalloc(sizeof(Pipe));
    File* r = (File*)kzalloc(sizeof(File));
    File* w = (File*)kzalloc(sizeof(File));
    if (!p || !r || !w) {
        kfree(p); kfree(r); kfree(w);
        return -FIH_ENOMEM;
    }
    p->readers = p->writers = 1;
    *r = {1, FIH_FT_PIPE, FIH_O_READ | (flags & FIH_O_NONBLOCK), nullptr, 0, 0, p};
    *w = {1, FIH_FT_PIPE, FIH_O_WRITE | (flags & FIH_O_NONBLOCK), nullptr, 0, 0, p};
    *rd = r;
    *wr = w;
    return 0;
}

void pipe_close_end(File* f) {
    Pipe* p = f->pipe;
    if (f->flags & FIH_O_READ) p->readers--;
    else p->writers--;
    proc_wake(p);
    if (!p->readers && !p->writers) kfree(p);
}

int pipe_read(File* f, void* buf, uint32_t n) {
    Pipe* p = f->pipe;
    if (n == 0) return 0;
    uint32_t irq = irq_save();
    while (p->count == 0) {
        if (p->writers == 0) { irq_restore(irq); return 0; }
        if (f->flags & FIH_O_NONBLOCK) { irq_restore(irq); return -FIH_EAGAIN; }
        if (proc_block(p, 0) < 0) { irq_restore(irq); return -FIH_EINTR; }
    }
    uint32_t got = kmin(n, p->count);
    auto* dst = static_cast<uint8_t*>(buf);
    for (uint32_t i = 0; i < got; i++) dst[i] = p->buf[(p->head + i) % sizeof(p->buf)];
    p->head = (p->head + got) % sizeof(p->buf);
    p->count -= got;
    proc_wake(p);
    irq_restore(irq);
    return (int)got;
}

int pipe_write(File* f, const void* buf, uint32_t n) {
    Pipe* p = f->pipe;
    auto* src = static_cast<const uint8_t*>(buf);
    uint32_t done = 0;
    uint32_t irq = irq_save();
    while (done < n) {
        if (p->readers == 0) { irq_restore(irq); return done ? (int)done : -FIH_EPIPE; }
        if (p->count == sizeof(p->buf)) {
            if (f->flags & FIH_O_NONBLOCK) break;
            if (proc_block(p, 0) < 0) break;
            continue;
        }
        uint32_t tail = (p->head + p->count) % sizeof(p->buf);
        p->buf[tail] = src[done++];
        p->count++;
    }
    proc_wake(p);
    irq_restore(irq);
    if (done == 0 && n) return (f->flags & FIH_O_NONBLOCK) ? -FIH_EAGAIN : -FIH_EINTR;
    return (int)done;
}
