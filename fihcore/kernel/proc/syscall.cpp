// System call entry: argument validation and dispatch.
#include "proc.hpp"
#include "arch/x86/cpu.hpp"
#include "dev/dev.hpp"
#include "fs/vfs.hpp"
#include "lib/lib.hpp"
#include "mm/heap.hpp"
#include "mm/pmm.hpp"
#include "mm/vmm.hpp"

extern char g_cpu_name[48];
uint32_t clock_epoch();

namespace {
constexpr int MAX_ARGS = 32;
constexpr uint32_t ARG_SPACE = 4096;

bool user_ok(uint32_t p, uint32_t n, bool write) { return vmm_user_ok(p, n, write); }

int copy_str(uint32_t up, char* out, uint32_t max) {
    for (uint32_t i = 0; i < max; i++) {
        if (i == 0 || ((up + i) & (PAGE_SIZE - 1)) == 0)
            if (!user_ok(up + i, 1, false)) return -FIH_EFAULT;
        out[i] = reinterpret_cast<const char*>(up)[i];
        if (!out[i]) return (int)i;
    }
    return -FIH_ENAMETOOLONG;
}

int user_path(uint32_t up, char abs[FIH_PATH_MAX]) {
    char raw[FIH_PATH_MAX];
    int n = copy_str(up, raw, sizeof(raw));
    if (n < 0) return n;
    return vfs_normalize(proc_current()->cwd, raw, abs);
}

File* fd_file(int fd) {
    if (fd < 0 || fd >= MAX_FDS) return nullptr;
    return proc_current()->fds[fd];
}

int fd_alloc(File* f, int from = 0) {
    Proc* p = proc_current();
    for (int i = from; i < MAX_FDS; i++)
        if (!p->fds[i]) { p->fds[i] = f; return i; }
    return -FIH_EMFILE;
}

int sys_spawn(uint32_t upath, uint32_t uargv, uint32_t ufds) {
    char path[FIH_PATH_MAX];
    int e = user_path(upath, path);
    if (e) return e;
    char* space = (char*)kmalloc(ARG_SPACE);
    if (!space) return -FIH_ENOMEM;
    const char* argv[MAX_ARGS];
    int argc = 0;
    uint32_t used = 0;
    if (uargv) {
        for (;; argc++) {
            if (!user_ok(uargv + argc * 4, 4, false)) { e = -FIH_EFAULT; break; }
            uint32_t s = reinterpret_cast<const uint32_t*>(uargv)[argc];
            if (!s) break;
            if (argc == MAX_ARGS) { e = -FIH_EINVAL; break; }
            int n = copy_str(s, space + used, ARG_SPACE - used);
            if (n < 0) { e = n == -FIH_ENAMETOOLONG ? -FIH_EINVAL : n; break; }
            argv[argc] = space + used;
            used += (uint32_t)n + 1;
        }
    }
    if (!e && argc == 0) argv[argc++] = path;
    File* stdio[3];
    Proc* p = proc_current();
    for (int i = 0; i < 3 && !e; i++) {
        int fd = i;
        if (ufds) {
            if (!user_ok(ufds, 12, false)) { e = -FIH_EFAULT; break; }
            fd = reinterpret_cast<const int*>(ufds)[i];
        }
        stdio[i] = (fd >= 0 && fd < MAX_FDS) ? p->fds[fd] : nullptr;
    }
    if (!e) e = proc_spawn(path, argv, argc, stdio);
    kfree(space);
    return e;
}

int sys_brk(uint32_t want) {
    Proc* p = proc_current();
    if (want == 0) return (int)p->brk_end;
    if (want < p->brk_start || want > USER_IMAGE_END) return -FIH_ENOMEM;
    uint32_t old_top = align_up(p->brk_end, PAGE_SIZE), new_top = align_up(want, PAGE_SIZE);
    if (new_top > old_top) {
        if ((new_top - old_top) / PAGE_SIZE + 256 > pmm_free_frames()) return -FIH_ENOMEM;
        int e = vmm_alloc_range(p->pd, old_top, new_top - old_top);
        if (e) return e;
        p->pages += (new_top - old_top) / PAGE_SIZE;
    } else {
        for (uint32_t a = new_top; a < old_top; a += PAGE_SIZE) vmm_unmap(p->pd, a);
        p->pages -= (old_top - new_top) / PAGE_SIZE;
    }
    p->brk_end = want;
    return (int)want;
}

int sys_open(uint32_t upath, uint32_t flags) {
    char path[FIH_PATH_MAX];
    int e = user_path(upath, path);
    if (e) return e;
    if (!(flags & (FIH_O_READ | FIH_O_WRITE))) flags |= FIH_O_READ;
    File* f;
    if ((e = vfs_open(path, flags, &f))) return e;
    int fd = fd_alloc(f);
    if (fd < 0) file_close(f);
    return fd;
}

int sys_rw(int fd, uint32_t buf, uint32_t n, bool write) {
    File* f = fd_file(fd);
    if (!f) return -FIH_EBADF;
    if (n > 0x100000) n = 0x100000;
    if (!user_ok(buf, n, !write)) return -FIH_EFAULT;
    return write ? file_write(f, (const void*)buf, n) : file_read(f, (void*)buf, n);
}

template <typename T> int put(uint32_t up, const T& v) {
    if (!user_ok(up, sizeof(T), true)) return -FIH_EFAULT;
    memcpy(reinterpret_cast<void*>(up), &v, sizeof(T));
    return 0;
}

int sys_stat(uint32_t upath, uint32_t ust) {
    char path[FIH_PATH_MAX];
    int e = user_path(upath, path);
    if (e) return e;
    fih_stat_t st;
    if ((e = vfs_stat(path, &st))) return e;
    return put(ust, st);
}

int sys_rename(uint32_t ufrom, uint32_t uto, int flags) {
    char a[FIH_PATH_MAX], b[FIH_PATH_MAX];
    int e = user_path(ufrom, a);
    if (!e) e = user_path(uto, b);
    if (e) return e;
    return vfs_rename(a, b, flags);
}

int sys_chdir(uint32_t upath) {
    char path[FIH_PATH_MAX];
    int e = user_path(upath, path);
    if (e) return e;
    fih_stat_t st;
    if ((e = vfs_stat(path, &st))) return e;
    if (st.type != FIH_FT_DIR) return -FIH_ENOTDIR;
    strlcpy(proc_current()->cwd, path, FIH_PATH_MAX);
    return 0;
}

int sys_getcwd(uint32_t buf, uint32_t size) {
    const char* cwd = proc_current()->cwd;
    uint32_t len = (uint32_t)strlen(cwd);
    if (size < len + 1) return -FIH_EINVAL;
    if (!user_ok(buf, len + 1, true)) return -FIH_EFAULT;
    memcpy((void*)buf, cwd, len + 1);
    return (int)len;
}

int sys_pipe(uint32_t ufds, uint32_t flags) {
    if (!user_ok(ufds, 8, true)) return -FIH_EFAULT;
    File *r, *w;
    int e = pipe_create(flags, &r, &w);
    if (e) return e;
    int a = fd_alloc(r), b = a >= 0 ? fd_alloc(w) : -1;
    if (a < 0 || b < 0) {
        if (a >= 0) proc_current()->fds[a] = nullptr;
        file_close(r);
        file_close(w);
        return -FIH_EMFILE;
    }
    reinterpret_cast<int*>(ufds)[0] = a;
    reinterpret_cast<int*>(ufds)[1] = b;
    return 0;
}

int sys_msg_send(uint32_t pid, uint32_t umsg) {
    if (!user_ok(umsg, sizeof(fih_msg_t), false)) return -FIH_EFAULT;
    fih_msg_t m;
    memcpy(&m, (const void*)umsg, sizeof(m));
    if (m.type < FIH_MSG_USER) return -FIH_EINVAL; // kernel message types cannot be forged
    m.from = proc_current()->pid;
    Proc* p = proc_by_pid(pid);
    if (!p) return -FIH_ESRCH;
    return mbox_push(p, m);
}

int sys_msg_recv(uint32_t umsg, int32_t timeout) {
    if (!user_ok(umsg, sizeof(fih_msg_t), true)) return -FIH_EFAULT;
    fih_msg_t m;
    int r = mbox_recv(&m, timeout);
    if (r == 1) memcpy((void*)umsg, &m, sizeof(m));
    return r;
}

int sys_sysinfo(uint32_t up) {
    fih_sysinfo_t si;
    memset(&si, 0, sizeof(si));
    si.abi_version = FIH_ABI_VERSION;
    strlcpy(si.kernel, "Fihcore " FIHCORE_VERSION, sizeof(si.kernel));
    si.mem_total_kb = pmm_total_frames() * 4;
    si.mem_free_kb = pmm_free_frames() * 4;
    si.heap_used_kb = heap_used() / 1024;
    si.heap_total_kb = heap_total() / 1024;
    si.uptime_ms = timer_ticks();
    Proc* t = proc_table();
    for (int i = 1; i < MAX_PROCS; i++)
        if (t[i].state != PState::Unused && t[i].state != PState::Dead) si.nprocs++;
    strlcpy(si.cpu, g_cpu_name, sizeof(si.cpu));
    return put(up, si);
}

int sys_proc_list(uint32_t up, uint32_t max) {
    if (max > MAX_PROCS) max = MAX_PROCS;
    if (!user_ok(up, max * sizeof(fih_procinfo_t), true)) return -FIH_EFAULT;
    auto* out = reinterpret_cast<fih_procinfo_t*>(up);
    Proc* t = proc_table();
    uint32_t n = 0;
    for (int i = 1; i < MAX_PROCS && n < max; i++) {
        Proc& p = t[i];
        if (p.state == PState::Unused || p.state == PState::Dead) continue;
        fih_procinfo_t& o = out[n++];
        memset(&o, 0, sizeof(o));
        o.pid = p.pid;
        o.ppid = p.ppid;
        o.state = p.state == PState::Running ? FIH_PROC_RUNNING
                : p.state == PState::Ready ? FIH_PROC_READY
                : p.state == PState::Blocked ? FIH_PROC_BLOCKED : FIH_PROC_ZOMBIE;
        o.mem_kb = p.pages * 4;
        o.cpu_ms = p.cpu_ticks;
        strlcpy(o.name, p.name, sizeof(o.name));
    }
    return (int)n;
}
}

void syscall_dispatch(Regs* r) {
    sti(); // system calls run with interrupts on; the kernel itself is not preempted
    uint32_t a = r->ebx, b = r->ecx, c = r->edx;
    Proc* p = proc_current();
    int ret = -FIH_ENOSYS;
    char path[FIH_PATH_MAX];
    switch (r->eax) {
    case FIH_SYS_EXIT: proc_exit_current((int)a);
    case FIH_SYS_SPAWN: ret = sys_spawn(a, b, c); break;
    case FIH_SYS_WAIT: {
        int code = 0;
        if (b && !user_ok(b, 4, true)) { ret = -FIH_EFAULT; break; }
        ret = proc_wait((int)a, &code, (int)c);
        if (ret > 0 && b) *reinterpret_cast<int*>(b) = code;
        break;
    }
    case FIH_SYS_KILL: ret = proc_kill(a); break;
    case FIH_SYS_GETPID: ret = (int)p->pid; break;
    case FIH_SYS_YIELD: schedule(); ret = 0; break;
    case FIH_SYS_SLEEP: proc_sleep(a); ret = 0; break;
    case FIH_SYS_BRK: ret = sys_brk(a); break;
    case FIH_SYS_OPEN: ret = sys_open(a, b); break;
    case FIH_SYS_CLOSE: {
        File* f = fd_file((int)a);
        if (!f) { ret = -FIH_EBADF; break; }
        p->fds[a] = nullptr;
        file_close(f);
        ret = 0;
        break;
    }
    case FIH_SYS_READ: ret = sys_rw((int)a, b, c, false); break;
    case FIH_SYS_WRITE: ret = sys_rw((int)a, b, c, true); break;
    case FIH_SYS_SEEK: {
        File* f = fd_file((int)a);
        ret = f ? file_seek(f, (int32_t)b, (int)c) : -FIH_EBADF;
        break;
    }
    case FIH_SYS_STAT: ret = sys_stat(a, b); break;
    case FIH_SYS_FSTAT: {
        File* f = fd_file((int)a);
        fih_stat_t st;
        ret = !f ? -FIH_EBADF : file_stat(f, &st);
        if (!ret) ret = put(b, st);
        break;
    }
    case FIH_SYS_READDIR: {
        File* f = fd_file((int)a);
        fih_dirent_t d;
        if (!f) { ret = -FIH_EBADF; break; }
        if (!user_ok(c, sizeof(d), true)) { ret = -FIH_EFAULT; break; }
        ret = file_readdir(f, b, &d);
        if (ret == 1) memcpy((void*)c, &d, sizeof(d));
        break;
    }
    case FIH_SYS_MKDIR: ret = user_path(a, path); if (!ret) ret = vfs_mkdir(path); break;
    case FIH_SYS_UNLINK: ret = user_path(a, path); if (!ret) ret = vfs_unlink(path); break;
    case FIH_SYS_RENAME: ret = sys_rename(a, b, (int)c); break;
    case FIH_SYS_TRUNCATE: {
        File* f = fd_file((int)a);
        ret = f ? file_truncate(f, b) : -FIH_EBADF;
        break;
    }
    case FIH_SYS_CHDIR: ret = sys_chdir(a); break;
    case FIH_SYS_GETCWD: ret = sys_getcwd(a, b); break;
    case FIH_SYS_PIPE: ret = sys_pipe(a, b); break;
    case FIH_SYS_DUP2: {
        File* f = fd_file((int)a);
        if (!f || (int)b < 0 || b >= (uint32_t)MAX_FDS) { ret = -FIH_EBADF; break; }
        if (a != b) {
            file_close(p->fds[b]);
            p->fds[b] = file_ref(f);
        }
        ret = (int)b;
        break;
    }
    case FIH_SYS_MSG_SEND: ret = sys_msg_send(a, b); break;
    case FIH_SYS_MSG_RECV: ret = sys_msg_recv(a, (int32_t)b); break;
    case FIH_SYS_SHM_CREATE: ret = shm_create(a); break;
    case FIH_SYS_SHM_MAP: {
        uint32_t va;
        ret = shm_map((int)a, &va);
        if (!ret) ret = (int)va;
        break;
    }
    case FIH_SYS_SHM_UNMAP: ret = shm_unmap((int)a); break;
    case FIH_SYS_SHM_GRANT: ret = shm_grant((int)a, b); break;
    case FIH_SYS_SHM_DESTROY: ret = shm_destroy((int)a); break;
    case FIH_SYS_SHM_SIZE: ret = shm_size((int)a); break;
    case FIH_SYS_DISPLAY_ACQUIRE: {
        fih_display_t d;
        ret = display_acquire(&d);
        if (!ret) ret = put(a, d);
        break;
    }
    case FIH_SYS_NAME_REGISTER:
        ret = copy_str(a, path, 32);
        if (ret >= 0) ret = name_register(path);
        break;
    case FIH_SYS_NAME_LOOKUP:
        ret = copy_str(a, path, 32);
        if (ret >= 0) ret = name_lookup(path);
        break;
    case FIH_SYS_SYSINFO: ret = sys_sysinfo(a); break;
    case FIH_SYS_PROC_LIST: ret = sys_proc_list(a, b); break;
    case FIH_SYS_STATFS: {
        fih_statfs_t sf;
        ret = user_path(a, path);
        if (!ret) ret = vfs_statfs(path, &sf);
        if (!ret) ret = put(b, sf);
        break;
    }
    case FIH_SYS_TIME: {
        fih_time_t t;
        rtc_read(&t);
        ret = put(a, t);
        break;
    }
    case FIH_SYS_UPTIME: ret = (int)timer_ticks(); break;
    case FIH_SYS_POWER:
        klog("power: request %u from pid %u\n", a, p->pid);
        for (int i = 0; i < ATA_MAX; i++) if (g_ata[i].present) ata_flush(i);
        if (a == FIH_POWER_REBOOT) power_reboot();
        ret = a == FIH_POWER_SHUTDOWN ? power_shutdown() : -FIH_EINVAL;
        break;
    case FIH_SYS_LOG: {
        uint32_t n = kmin<uint32_t>(b, 1024);
        if (!user_ok(a, n, false)) { ret = -FIH_EFAULT; break; }
        klog("[%s:%u] ", p->name, p->pid);
        klog_write((const char*)a, n);
        if (n && ((const char*)a)[n - 1] != '\n') klog_write("\n", 1);
        ret = 0;
        break;
    }
    }
    r->eax = (uint32_t)ret;
}
