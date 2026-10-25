/*
 * Fihcore kernel ABI.
 *
 * This header is the single authoritative definition of the interface between
 * the Fihcore kernel and user space: system call numbers, argument structures,
 * error codes and kernel-generated message types. It must stay valid C and C++
 * and must not depend on anything but <stdint.h>.
 *
 * Calling convention (i386): `int $0x80`, eax = call number, arguments in
 * ebx, ecx, edx, esi, edi. The result is returned in eax; negative values are
 * -FIH_E* error codes.
 */
#ifndef FIHCORE_ABI_H
#define FIHCORE_ABI_H

#include <stdint.h>

#define FIH_ABI_VERSION 1

/* ---- System call numbers ------------------------------------------------ */
enum {
    FIH_SYS_EXIT = 1,        /* (int code) -> never returns */
    FIH_SYS_SPAWN,           /* (const char* path, const char* const* argv, const int* fds3) -> pid */
    FIH_SYS_WAIT,            /* (int pid, int* code, int flags) -> pid | 0 (FIH_WNOHANG, still running) */
    FIH_SYS_KILL,            /* (int pid) -> 0 */
    FIH_SYS_GETPID,          /* () -> pid */
    FIH_SYS_YIELD,           /* () -> 0 */
    FIH_SYS_SLEEP,           /* (uint32_t ms) -> 0 */
    FIH_SYS_BRK,             /* (uint32_t new_end | 0) -> current end */
    FIH_SYS_OPEN,            /* (const char* path, int flags) -> fd */
    FIH_SYS_CLOSE,           /* (int fd) -> 0 */
    FIH_SYS_READ,            /* (int fd, void* buf, uint32_t n) -> bytes */
    FIH_SYS_WRITE,           /* (int fd, const void* buf, uint32_t n) -> bytes */
    FIH_SYS_SEEK,            /* (int fd, int32_t off, int whence) -> new position */
    FIH_SYS_STAT,            /* (const char* path, fih_stat_t* st) -> 0 */
    FIH_SYS_FSTAT,           /* (int fd, fih_stat_t* st) -> 0 */
    FIH_SYS_READDIR,         /* (int fd, uint32_t index, fih_dirent_t* out) -> 1 entry, 0 end */
    FIH_SYS_MKDIR,           /* (const char* path) -> 0 */
    FIH_SYS_UNLINK,          /* (const char* path) -> 0; removes files and empty directories */
    FIH_SYS_RENAME,          /* (const char* from, const char* to, int flags) -> 0 */
    FIH_SYS_TRUNCATE,        /* (int fd, uint32_t size) -> 0 */
    FIH_SYS_CHDIR,           /* (const char* path) -> 0 */
    FIH_SYS_GETCWD,          /* (char* buf, uint32_t size) -> length */
    FIH_SYS_PIPE,            /* (int fds[2], int flags) -> 0; fds[0] read end, fds[1] write end */
    FIH_SYS_DUP2,            /* (int oldfd, int newfd) -> newfd */
    FIH_SYS_MSG_SEND,        /* (int pid, const fih_msg_t* msg) -> 0 */
    FIH_SYS_MSG_RECV,        /* (fih_msg_t* out, int32_t timeout_ms) -> 1 message, 0 timeout. -1 waits forever */
    FIH_SYS_SHM_CREATE,      /* (uint32_t size) -> shm id */
    FIH_SYS_SHM_MAP,         /* (int id) -> user address */
    FIH_SYS_SHM_UNMAP,       /* (int id) -> 0 */
    FIH_SYS_SHM_GRANT,       /* (int id, int pid) -> 0 */
    FIH_SYS_SHM_DESTROY,     /* (int id) -> 0; freed once no process maps it */
    FIH_SYS_DISPLAY_ACQUIRE, /* (fih_display_t* out) -> 0; exclusive framebuffer + input */
    FIH_SYS_NAME_REGISTER,   /* (const char* name) -> 0 */
    FIH_SYS_NAME_LOOKUP,     /* (const char* name) -> pid */
    FIH_SYS_SYSINFO,         /* (fih_sysinfo_t* out) -> 0 */
    FIH_SYS_PROC_LIST,       /* (fih_procinfo_t* out, uint32_t max) -> count */
    FIH_SYS_STATFS,          /* (const char* path, fih_statfs_t* out) -> 0 */
    FIH_SYS_TIME,            /* (fih_time_t* out) -> 0 */
    FIH_SYS_UPTIME,          /* () -> milliseconds since boot */
    FIH_SYS_POWER,           /* (int action) -> error only (FIH_POWER_*) */
    FIH_SYS_LOG,             /* (const char* text, uint32_t len) -> 0; kernel log */
    FIH_SYS_SHM_SIZE,        /* (int id) -> size in bytes, for objects the caller may map */
    FIH_SYS_COUNT
};

/* ---- Error codes (returned negated) ------------------------------------- */
enum {
    FIH_OK = 0,
    FIH_EPERM = 1,
    FIH_ENOENT = 2,
    FIH_ESRCH = 3,
    FIH_EINTR = 4,
    FIH_EIO = 5,
    FIH_ENOEXEC = 8,
    FIH_EBADF = 9,
    FIH_ECHILD = 10,
    FIH_EAGAIN = 11,
    FIH_ENOMEM = 12,
    FIH_EFAULT = 14,
    FIH_EBUSY = 16,
    FIH_EEXIST = 17,
    FIH_EXDEV = 18,
    FIH_ENODEV = 19,
    FIH_ENOTDIR = 20,
    FIH_EISDIR = 21,
    FIH_EINVAL = 22,
    FIH_EMFILE = 24,
    FIH_EFBIG = 27,
    FIH_ENOSPC = 28,
    FIH_ESPIPE = 29,
    FIH_EROFS = 30,
    FIH_EPIPE = 32,
    FIH_ENAMETOOLONG = 36,
    FIH_ENOSYS = 38,
    FIH_ENOTEMPTY = 39,
    FIH_ECORRUPT = 117
};

/* ---- Flags --------------------------------------------------------------- */
#define FIH_O_READ     0x01
#define FIH_O_WRITE    0x02
#define FIH_O_CREATE   0x04
#define FIH_O_TRUNC    0x08
#define FIH_O_APPEND   0x10
#define FIH_O_EXCL     0x20
#define FIH_O_NONBLOCK 0x40

#define FIH_SEEK_SET 0
#define FIH_SEEK_CUR 1
#define FIH_SEEK_END 2

#define FIH_WNOHANG 1

#define FIH_RENAME_REPLACE 1 /* atomically replace an existing regular file */

#define FIH_POWER_REBOOT   1
#define FIH_POWER_SHUTDOWN 2

#define FIH_NAME_MAX 57  /* maximum file name length */
#define FIH_PATH_MAX 256 /* maximum path length including NUL */

/* ---- Structures ---------------------------------------------------------- */
enum { FIH_FT_FILE = 1, FIH_FT_DIR = 2, FIH_FT_PIPE = 3, FIH_FT_CONSOLE = 4 };

#define FIH_ST_READONLY 1

typedef struct {
    uint32_t type;   /* FIH_FT_* */
    uint32_t size;   /* bytes */
    uint32_t mtime;  /* seconds since 2000-01-01 00:00 */
    uint32_t ctime;
    uint32_t inode;
    uint32_t flags;  /* FIH_ST_* */
} fih_stat_t;

typedef struct {
    char name[FIH_NAME_MAX + 3];
    uint32_t type;
    uint32_t size;
} fih_dirent_t;

/* IPC message: fixed 64 bytes. `from` and the kernel-owned types are filled
 * in by the kernel; user processes may use any type >= FIH_MSG_USER. */
typedef struct {
    uint32_t from;
    uint32_t type;
    uint32_t a[14];
} fih_msg_t;

enum {
    FIH_MSG_KEY = 1,        /* a0 keycode, a1 pressed, a2 character, a3 modifiers */
    FIH_MSG_MOUSE = 2,      /* a0 dx, a1 dy (screen down positive), a2 buttons, a3 wheel */
    FIH_MSG_CHILD_EXIT = 3, /* a0 pid, a1 exit code */
    FIH_MSG_USER = 0x100
};

/* Key modifiers (FIH_MSG_KEY a3) */
#define FIH_MOD_SHIFT 0x01
#define FIH_MOD_CTRL  0x02
#define FIH_MOD_ALT   0x04
#define FIH_MOD_SUPER 0x08
#define FIH_MOD_CAPS  0x10

/* Key codes: PS/2 scan code set 1 make codes, 0x100 added for E0-prefixed keys. */
enum {
    FIH_KEY_ESC = 0x01, FIH_KEY_BACKSPACE = 0x0E, FIH_KEY_TAB = 0x0F, FIH_KEY_ENTER = 0x1C,
    FIH_KEY_LCTRL = 0x1D, FIH_KEY_LSHIFT = 0x2A, FIH_KEY_RSHIFT = 0x36, FIH_KEY_LALT = 0x38,
    FIH_KEY_SPACE = 0x39, FIH_KEY_CAPS = 0x3A,
    FIH_KEY_F1 = 0x3B, FIH_KEY_F2, FIH_KEY_F3, FIH_KEY_F4, FIH_KEY_F5, FIH_KEY_F6,
    FIH_KEY_F7, FIH_KEY_F8, FIH_KEY_F9, FIH_KEY_F10, FIH_KEY_F11 = 0x57, FIH_KEY_F12 = 0x58,
    FIH_KEY_RCTRL = 0x11D, FIH_KEY_RALT = 0x138,
    FIH_KEY_HOME = 0x147, FIH_KEY_UP = 0x148, FIH_KEY_PGUP = 0x149, FIH_KEY_LEFT = 0x14B,
    FIH_KEY_RIGHT = 0x14D, FIH_KEY_END = 0x14F, FIH_KEY_DOWN = 0x150, FIH_KEY_PGDN = 0x151,
    FIH_KEY_INSERT = 0x152, FIH_KEY_DELETE = 0x153, FIH_KEY_LSUPER = 0x15B, FIH_KEY_RSUPER = 0x15C
};

#define FIH_MOUSE_LEFT   1
#define FIH_MOUSE_RIGHT  2
#define FIH_MOUSE_MIDDLE 4

typedef struct {
    uint32_t vaddr;  /* framebuffer address in the caller's address space */
    uint32_t width;
    uint32_t height;
    uint32_t pitch;  /* bytes per row */
    uint32_t bpp;    /* always 32 for now */
} fih_display_t;

typedef struct {
    uint32_t abi_version;
    char kernel[32];      /* e.g. "Fihcore 0.1.0" */
    uint32_t mem_total_kb;
    uint32_t mem_free_kb;
    uint32_t heap_used_kb;
    uint32_t heap_total_kb;
    uint32_t uptime_ms;
    uint32_t nprocs;
    char cpu[48];
} fih_sysinfo_t;

enum { FIH_PROC_READY = 1, FIH_PROC_RUNNING, FIH_PROC_BLOCKED, FIH_PROC_ZOMBIE };

typedef struct {
    uint32_t pid;
    uint32_t ppid;
    uint32_t state;
    uint32_t mem_kb;
    uint32_t cpu_ms;
    char name[32];
} fih_procinfo_t;

typedef struct {
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t total_inodes;
    uint32_t free_inodes;
    uint32_t flags;      /* FIH_ST_READONLY */
    char fstype[16];
    char device[32];
} fih_statfs_t;

typedef struct {
    uint16_t year;
    uint8_t month, day, hour, minute, second, _pad;
} fih_time_t;

#endif
