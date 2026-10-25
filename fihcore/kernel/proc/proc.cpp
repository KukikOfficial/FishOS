#include "proc.hpp"
#include "arch/x86/cpu.hpp"
#include "dev/dev.hpp"
#include "fs/vfs.hpp"
#include "lib/lib.hpp"
#include "mm/heap.hpp"
#include "mm/pmm.hpp"
#include "mm/vmm.hpp"

extern "C" void context_switch(uint32_t* save, uint32_t new_esp, uint32_t new_cr3);
extern "C" void isr_return();
extern "C" uint8_t boot_stack_top[];

namespace {
Proc procs[MAX_PROCS];
Proc* current;
uint32_t next_pid = 1;
bool need_resched;
uint8_t fpu_initial[108];
constexpr uint32_t QUANTUM_MS = 10;

Proc* alloc_slot() {
    for (int i = 1; i < MAX_PROCS; i++) {
        Proc& p = procs[i];
        if (p.state == PState::Dead && &p != current) { // stack no longer in use
            kfree(p.kstack);
            p.state = PState::Unused;
        }
        if (p.state == PState::Unused) {
            memset(&p, 0, sizeof(p));
            return &p;
        }
    }
    return nullptr;
}

void release(Proc* p) { // final cleanup of a zombie
    if (p == current) p->state = PState::Dead;
    else {
        kfree(p->kstack);
        p->state = PState::Unused;
    }
}

// Copies bytes into another address space through the identity map.
int copy_out(uint32_t pd, uint32_t va, const void* src, uint32_t n) {
    auto* s = static_cast<const uint8_t*>(src);
    while (n) {
        uint32_t pte = vmm_pte(pd, va);
        if (!(pte & PTE_PRESENT)) return -FIH_EFAULT;
        uint32_t off = va & (PAGE_SIZE - 1), chunk = kmin(n, PAGE_SIZE - off);
        memcpy(reinterpret_cast<uint8_t*>((pte & ~0xFFFu) + off), s, chunk);
        s += chunk;
        va += chunk;
        n -= chunk;
    }
    return 0;
}
}

void proc_init() {
    memset(procs, 0, sizeof(procs));
    Proc& idle = procs[0];
    idle.pid = 0;
    idle.state = PState::Running;
    strlcpy(idle.name, "idle", sizeof(idle.name));
    strlcpy(idle.cwd, "/", sizeof(idle.cwd));
    current = &idle;
    gdt_set_kernel_stack((uint32_t)boot_stack_top);
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 = (cr0 & ~0x0Cu) | 0x22; // EM=0 TS=0 MP=1 NE=1
    asm volatile("mov %0, %%cr0; fninit; fnsave %1" : : "r"(cr0), "m"(fpu_initial) : "memory");
    memcpy(idle.fpu, fpu_initial, sizeof(fpu_initial));
}

Proc* proc_current() { return current; }
Proc* proc_table() { return procs; }

Proc* proc_by_pid(uint32_t pid) {
    for (int i = 1; i < MAX_PROCS; i++) {
        PState s = procs[i].state;
        if (procs[i].pid == pid && s != PState::Unused && s != PState::Dead) return &procs[i];
    }
    return nullptr;
}

int proc_spawn(const char* path, const char* const* argv, int argc, File* const stdio[3]) {
    File* f;
    int e = vfs_open(path, FIH_O_READ, &f);
    if (e) return e;
    if (f->type != FIH_FT_FILE) {
        file_close(f);
        return -FIH_ENOEXEC;
    }
    Proc* p = alloc_slot();
    if (!p) {
        file_close(f);
        return -FIH_EAGAIN;
    }
    p->kstack = (uint8_t*)kmalloc(KSTACK_SIZE);
    p->mbox = (fih_msg_t*)kmalloc(sizeof(fih_msg_t) * MAILBOX_CAP);
    p->pd = vmm_create_space();
    uint32_t entry = 0, image_end = 0;
    e = (!p->kstack || !p->mbox || !p->pd) ? -FIH_ENOMEM : elf_load(p->pd, f, &entry, &image_end, &p->pages);
    file_close(f);

    // User stack: argument strings, then argv[], then (argc, argv) for _start.
    uint32_t sp = USER_STACK_TOP;
    uint32_t uargv[33];
    if (!e) e = vmm_alloc_range(p->pd, USER_STACK_TOP - 0x10000, 0x10000);
    if (!e) p->pages += 16;
    if (argc > 32) e = -FIH_EINVAL;
    for (int i = argc - 1; !e && i >= 0; i--) {
        uint32_t len = (uint32_t)strlen(argv[i]) + 1;
        if (USER_STACK_TOP - sp + len > 0xC000) { e = -FIH_EINVAL; break; }
        sp -= len;
        e = copy_out(p->pd, sp, argv[i], len);
        uargv[i] = sp;
    }
    uargv[argc < 33 ? argc : 32] = 0;
    sp &= ~15u;
    if (!e) {
        sp -= (uint32_t)(argc + 1) * 4;
        e = copy_out(p->pd, sp, uargv, (uint32_t)(argc + 1) * 4);
        // cdecl entry: return address, argc, argv, with argc 16-byte aligned.
        uint32_t base = (sp - 8) & ~15u;
        uint32_t frame[3] = {0, (uint32_t)argc, sp};
        sp = base - 4;
        if (!e) e = copy_out(p->pd, sp, frame, sizeof(frame));
    }
    if (e) {
        if (p->pd) vmm_destroy_space(p->pd);
        kfree(p->kstack);
        kfree(p->mbox);
        p->state = PState::Unused;
        return e;
    }

    const char* base = path;
    for (const char* c = path; *c; c++) if (*c == '/') base = c + 1;
    strlcpy(p->name, base, sizeof(p->name));
    strlcpy(p->cwd, current->cwd, sizeof(p->cwd));
    p->pid = next_pid++;
    p->ppid = current->pid;
    p->brk_start = p->brk_end = align_up(image_end, PAGE_SIZE);
    for (int i = 0; i < 3; i++) p->fds[i] = file_ref(stdio[i]);
    memcpy(p->fpu, fpu_initial, sizeof(fpu_initial));

    // Initial kernel stack: an interrupt frame that isr_return irets to ring 3
    // with, below it the frame context_switch pops.
    uint32_t* k = reinterpret_cast<uint32_t*>(p->kstack + KSTACK_SIZE);
    *--k = SEL_UDATA;  // ss
    *--k = sp;         // esp
    *--k = 0x202;      // eflags: IF
    *--k = SEL_UCODE;  // cs
    *--k = entry;      // eip
    *--k = 0;          // err
    *--k = 0;          // int_no
    for (int i = 0; i < 8; i++) *--k = 0; // pusha block
    for (int i = 0; i < 4; i++) *--k = SEL_UDATA; // ds es fs gs
    *--k = (uint32_t)isr_return;
    for (int i = 0; i < 4; i++) *--k = 0; // ebp ebx esi edi
    *--k = 0x002;      // eflags for popf: interrupts off until iret
    p->kesp = (uint32_t)k;
    p->state = PState::Ready;
    klog("proc: started pid %u '%s' (parent %u)\n", p->pid, p->name, p->ppid);
    return (int)p->pid;
}

void proc_exit_current(int code) {
    Proc* p = current;
    KASSERT(p->pid != 0);
    sti();
    for (int i = 0; i < MAX_FDS; i++) {
        file_close(p->fds[i]);
        p->fds[i] = nullptr;
    }
    ipc_release(p);
    cli();
    write_cr3(vmm_kernel_pd());
    vmm_destroy_space(p->pd);
    p->pd = 0;
    kfree(p->mbox);
    p->mbox = nullptr;
    p->exit_code = code;
    klog("proc: pid %u '%s' exited with %d\n", p->pid, p->name, code);

    for (int i = 1; i < MAX_PROCS; i++) { // orphans are reaped by nobody: auto-release
        Proc& c = procs[i];
        if (c.ppid != p->pid || c.state == PState::Unused || c.state == PState::Dead) continue;
        c.ppid = 0;
        if (c.state == PState::Zombie) release(&c);
    }
    Proc* parent = p->ppid ? proc_by_pid(p->ppid) : nullptr;
    if (parent && parent->state != PState::Zombie) {
        p->state = PState::Zombie;
        fih_msg_t m{};
        m.type = FIH_MSG_CHILD_EXIT;
        m.a[0] = p->pid;
        m.a[1] = (uint32_t)code;
        mbox_push(parent, m);
        proc_wake(parent);
    } else {
        p->state = PState::Dead;
    }
    schedule();
    panic("exited process resumed");
}

int proc_wait(int pid, int* code, int flags) {
    uint32_t irq = irq_save();
    for (;;) {
        bool have_child = false;
        for (int i = 1; i < MAX_PROCS; i++) {
            Proc& c = procs[i];
            if (c.ppid != current->pid || c.state == PState::Unused || c.state == PState::Dead) continue;
            if (pid > 0 && c.pid != (uint32_t)pid) continue;
            have_child = true;
            if (c.state == PState::Zombie) {
                int cpid = (int)c.pid;
                if (code) *code = c.exit_code;
                release(&c);
                irq_restore(irq);
                return cpid;
            }
        }
        if (!have_child) { irq_restore(irq); return -FIH_ECHILD; }
        if (flags & FIH_WNOHANG) { irq_restore(irq); return 0; }
        if (proc_block(current, 0) < 0) { irq_restore(irq); return -FIH_EINTR; }
    }
}

int proc_kill(uint32_t pid) {
    Proc* p = proc_by_pid(pid);
    if (!p || p->pid == 0) return -FIH_ESRCH;
    if (p == current) proc_exit_current(-9);
    uint32_t irq = irq_save();
    p->killed = true;
    if (p->state == PState::Blocked) p->state = PState::Ready;
    irq_restore(irq);
    return 0;
}

int proc_block(const void* obj, uint32_t timeout_ms) {
    Proc* p = current;
    if (p->killed) return -FIH_EINTR;
    p->wait_obj = obj;
    p->timed_out = false;
    p->wake_at = timeout_ms ? timer_ticks() + timeout_ms : 0;
    if (p->wake_at == 0 && timeout_ms) p->wake_at = 1;
    p->state = PState::Blocked;
    schedule();
    p->wait_obj = nullptr;
    p->wake_at = 0;
    return p->killed ? -FIH_EINTR : 0;
}

void proc_wake(const void* obj) {
    uint32_t irq = irq_save();
    for (int i = 1; i < MAX_PROCS; i++) {
        if (procs[i].state == PState::Blocked && procs[i].wait_obj == obj) {
            procs[i].state = PState::Ready;
            need_resched = true;
        }
    }
    irq_restore(irq);
}

void proc_sleep(uint32_t ms) {
    uint32_t irq = irq_save();
    static const char sleep_token = 0;
    proc_block(&sleep_token, ms ? ms : 1);
    irq_restore(irq);
}

void schedule() {
    uint32_t irq = irq_save();
    Proc* prev = current;
    int start = (int)(prev - procs);
    Proc* next = nullptr;
    for (int n = 1; n <= MAX_PROCS; n++) {
        Proc& c = procs[(start + n) % MAX_PROCS];
        if (&c != &procs[0] && c.state == PState::Ready) { next = &c; break; }
    }
    if (!next) next = prev->state == PState::Running ? prev : &procs[0];
    need_resched = false;
    if (next == prev) { irq_restore(irq); return; }
    if (prev->state == PState::Running) prev->state = PState::Ready;
    next->state = PState::Running;
    next->quantum = QUANTUM_MS;
    current = next;
    gdt_set_kernel_stack(next == &procs[0] ? (uint32_t)boot_stack_top : (uint32_t)(next->kstack + KSTACK_SIZE));
    // The kernel never uses the FPU (-mgeneral-regs-only), so user x87 state
    // only has to be swapped here.
    asm volatile("fnsave %0" : "=m"(prev->fpu) : : "memory");
    asm volatile("frstor %0" : : "m"(next->fpu) : "memory");
    context_switch(&prev->kesp, next->kesp, next->pd ? next->pd : vmm_kernel_pd());
    irq_restore(irq);
}

void sched_tick(Regs*) {
    uint32_t now = timer_ticks();
    current->cpu_ticks++;
    for (int i = 1; i < MAX_PROCS; i++) {
        Proc& p = procs[i];
        if (p.state == PState::Blocked && p.wake_at && (int32_t)(now - p.wake_at) >= 0) {
            p.timed_out = true;
            p.state = PState::Ready;
            need_resched = true;
        }
    }
    if (current->quantum && --current->quantum == 0) need_resched = true;
}

void sched_return_to_user(Regs*) {
    if (current->killed) proc_exit_current(-9);
    if (need_resched) schedule();
    if (current->killed) proc_exit_current(-9);
}

void sched_idle_loop() {
    for (;;) {
        cli();
        // Release slots of processes that exited without a waiting parent.
        for (int i = 1; i < MAX_PROCS; i++)
            if (procs[i].state == PState::Dead && &procs[i] != current) {
                kfree(procs[i].kstack);
                procs[i].kstack = nullptr;
                procs[i].state = PState::Unused;
            }
        schedule();
        asm volatile("sti; hlt");
    }
}
