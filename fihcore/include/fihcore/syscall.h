/*
 * Thin user-space wrappers for the Fihcore system calls (see abi.h).
 * Header-only so any user-space environment can use it without a library.
 */
#ifndef FIHCORE_SYSCALL_H
#define FIHCORE_SYSCALL_H

#include <fihcore/abi.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int fih_syscall(uint32_t n, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    int r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory");
    return r;
}

#define FIH_U(x) ((uint32_t)(x))

static inline __attribute__((noreturn)) void fih_exit(int code) {
    fih_syscall(FIH_SYS_EXIT, FIH_U(code), 0, 0, 0, 0);
    for (;;) {}
}
static inline int fih_spawn(const char* path, const char* const* argv, const int* fds3) {
    return fih_syscall(FIH_SYS_SPAWN, FIH_U(path), FIH_U(argv), FIH_U(fds3), 0, 0);
}
static inline int fih_wait(int pid, int* code, int flags) { return fih_syscall(FIH_SYS_WAIT, FIH_U(pid), FIH_U(code), FIH_U(flags), 0, 0); }
static inline int fih_kill(int pid) { return fih_syscall(FIH_SYS_KILL, FIH_U(pid), 0, 0, 0, 0); }
static inline int fih_getpid(void) { return fih_syscall(FIH_SYS_GETPID, 0, 0, 0, 0, 0); }
static inline int fih_yield(void) { return fih_syscall(FIH_SYS_YIELD, 0, 0, 0, 0, 0); }
static inline int fih_sleep(uint32_t ms) { return fih_syscall(FIH_SYS_SLEEP, ms, 0, 0, 0, 0); }
static inline int fih_brk(uint32_t end) { return fih_syscall(FIH_SYS_BRK, end, 0, 0, 0, 0); }
static inline int fih_open(const char* path, int flags) { return fih_syscall(FIH_SYS_OPEN, FIH_U(path), FIH_U(flags), 0, 0, 0); }
static inline int fih_close(int fd) { return fih_syscall(FIH_SYS_CLOSE, FIH_U(fd), 0, 0, 0, 0); }
static inline int fih_read(int fd, void* buf, uint32_t n) { return fih_syscall(FIH_SYS_READ, FIH_U(fd), FIH_U(buf), n, 0, 0); }
static inline int fih_write(int fd, const void* buf, uint32_t n) { return fih_syscall(FIH_SYS_WRITE, FIH_U(fd), FIH_U(buf), n, 0, 0); }
static inline int fih_seek(int fd, int32_t off, int whence) { return fih_syscall(FIH_SYS_SEEK, FIH_U(fd), FIH_U(off), FIH_U(whence), 0, 0); }
static inline int fih_stat(const char* path, fih_stat_t* st) { return fih_syscall(FIH_SYS_STAT, FIH_U(path), FIH_U(st), 0, 0, 0); }
static inline int fih_fstat(int fd, fih_stat_t* st) { return fih_syscall(FIH_SYS_FSTAT, FIH_U(fd), FIH_U(st), 0, 0, 0); }
static inline int fih_readdir(int fd, uint32_t index, fih_dirent_t* out) { return fih_syscall(FIH_SYS_READDIR, FIH_U(fd), index, FIH_U(out), 0, 0); }
static inline int fih_mkdir(const char* path) { return fih_syscall(FIH_SYS_MKDIR, FIH_U(path), 0, 0, 0, 0); }
static inline int fih_unlink(const char* path) { return fih_syscall(FIH_SYS_UNLINK, FIH_U(path), 0, 0, 0, 0); }
static inline int fih_rename(const char* from, const char* to, int flags) { return fih_syscall(FIH_SYS_RENAME, FIH_U(from), FIH_U(to), FIH_U(flags), 0, 0); }
static inline int fih_truncate(int fd, uint32_t size) { return fih_syscall(FIH_SYS_TRUNCATE, FIH_U(fd), size, 0, 0, 0); }
static inline int fih_chdir(const char* path) { return fih_syscall(FIH_SYS_CHDIR, FIH_U(path), 0, 0, 0, 0); }
static inline int fih_getcwd(char* buf, uint32_t size) { return fih_syscall(FIH_SYS_GETCWD, FIH_U(buf), size, 0, 0, 0); }
static inline int fih_pipe(int fds[2], int flags) { return fih_syscall(FIH_SYS_PIPE, FIH_U(fds), FIH_U(flags), 0, 0, 0); }
static inline int fih_dup2(int oldfd, int newfd) { return fih_syscall(FIH_SYS_DUP2, FIH_U(oldfd), FIH_U(newfd), 0, 0, 0); }
static inline int fih_msg_send(int pid, const fih_msg_t* m) { return fih_syscall(FIH_SYS_MSG_SEND, FIH_U(pid), FIH_U(m), 0, 0, 0); }
static inline int fih_msg_recv(fih_msg_t* m, int32_t timeout_ms) { return fih_syscall(FIH_SYS_MSG_RECV, FIH_U(m), FIH_U(timeout_ms), 0, 0, 0); }
static inline int fih_shm_create(uint32_t size) { return fih_syscall(FIH_SYS_SHM_CREATE, size, 0, 0, 0, 0); }
static inline void* fih_shm_map(int id) {
    int r = fih_syscall(FIH_SYS_SHM_MAP, FIH_U(id), 0, 0, 0, 0);
    return (r < 0 && r > -4096) ? (void*)0 : (void*)(uint32_t)r;
}
static inline int fih_shm_unmap(int id) { return fih_syscall(FIH_SYS_SHM_UNMAP, FIH_U(id), 0, 0, 0, 0); }
static inline int fih_shm_grant(int id, int pid) { return fih_syscall(FIH_SYS_SHM_GRANT, FIH_U(id), FIH_U(pid), 0, 0, 0); }
static inline int fih_shm_size(int id) { return fih_syscall(FIH_SYS_SHM_SIZE, FIH_U(id), 0, 0, 0, 0); }
static inline int fih_shm_destroy(int id) { return fih_syscall(FIH_SYS_SHM_DESTROY, FIH_U(id), 0, 0, 0, 0); }
static inline int fih_display_acquire(fih_display_t* out) { return fih_syscall(FIH_SYS_DISPLAY_ACQUIRE, FIH_U(out), 0, 0, 0, 0); }
static inline int fih_name_register(const char* name) { return fih_syscall(FIH_SYS_NAME_REGISTER, FIH_U(name), 0, 0, 0, 0); }
static inline int fih_name_lookup(const char* name) { return fih_syscall(FIH_SYS_NAME_LOOKUP, FIH_U(name), 0, 0, 0, 0); }
static inline int fih_sysinfo(fih_sysinfo_t* out) { return fih_syscall(FIH_SYS_SYSINFO, FIH_U(out), 0, 0, 0, 0); }
static inline int fih_proc_list(fih_procinfo_t* out, uint32_t max) { return fih_syscall(FIH_SYS_PROC_LIST, FIH_U(out), max, 0, 0, 0); }
static inline int fih_statfs(const char* path, fih_statfs_t* out) { return fih_syscall(FIH_SYS_STATFS, FIH_U(path), FIH_U(out), 0, 0, 0); }
static inline int fih_time(fih_time_t* out) { return fih_syscall(FIH_SYS_TIME, FIH_U(out), 0, 0, 0, 0); }
static inline uint32_t fih_uptime(void) { return (uint32_t)fih_syscall(FIH_SYS_UPTIME, 0, 0, 0, 0, 0); }
static inline int fih_power(int action) { return fih_syscall(FIH_SYS_POWER, FIH_U(action), 0, 0, 0, 0); }
static inline int fih_log(const char* text, uint32_t len) { return fih_syscall(FIH_SYS_LOG, FIH_U(text), len, 0, 0, 0); }

#undef FIH_U

#ifdef __cplusplus
}
#endif
#endif
