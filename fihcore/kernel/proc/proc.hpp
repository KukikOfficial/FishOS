// Processes, scheduling and blocking.
#pragma once
#include <stdint.h>
#include <fihcore/abi.h>

struct Regs;
struct File;

constexpr int MAX_PROCS = 64;
constexpr int MAX_FDS = 32;
constexpr int MAILBOX_CAP = 128;
constexpr int MAX_MAPS = 32;
constexpr uint32_t KSTACK_SIZE = 16384;

enum class PState : uint8_t { Unused, Ready, Running, Blocked, Zombie, Dead };

struct Mapping {
    uint32_t va, pages;
    int shm; // shared memory id, or 0 for the framebuffer
};

struct Proc {
    uint32_t pid, ppid;
    PState state;
    bool killed;
    bool timed_out;
    char name[32];
    uint32_t pd;          // page directory (physical); 0 for kernel threads
    uint8_t* kstack;
    uint32_t kesp;
    uint32_t brk_start, brk_end;
    uint32_t pages;       // private user pages, for accounting
    uint32_t quantum;
    uint32_t cpu_ticks;
    int exit_code;
    const void* wait_obj;
    uint32_t wake_at;     // tick to wake at, 0 = no timeout
    File* fds[MAX_FDS];
    char cwd[FIH_PATH_MAX];
    fih_msg_t* mbox;
    uint32_t mb_head, mb_count;
    Mapping maps[MAX_MAPS];
    uint8_t fpu[108]; // x87 state (fnsave image)
};

void proc_init();
Proc* proc_current();
Proc* proc_by_pid(uint32_t pid);
Proc* proc_table();

// Loads `path` as a new user process. argv/stdio live in kernel memory.
int proc_spawn(const char* path, const char* const* argv, int argc, File* const stdio[3]);
[[noreturn]] void proc_exit_current(int code);
int proc_wait(int pid, int* code, int flags);
int proc_kill(uint32_t pid);

// Blocking. Call with interrupts disabled; returns -FIH_EINTR if the process
// was killed while waiting and 0 otherwise (check timed_out for timeouts).
int proc_block(const void* obj, uint32_t timeout_ms);
void proc_wake(const void* obj);
void proc_sleep(uint32_t ms);

void schedule();
void sched_tick(Regs* r);
void sched_return_to_user(Regs* r);
[[noreturn]] void sched_idle_loop();

// IPC (ipc.cpp)
int mbox_push(Proc* p, const fih_msg_t& m);
int mbox_recv(fih_msg_t* out, int32_t timeout_ms);
void ipc_release(Proc* p); // names, shared memory, display on exit
int shm_create(uint32_t size);
int shm_map(int id, uint32_t* va);
int shm_unmap(int id);
int shm_grant(int id, uint32_t pid);
int shm_destroy(int id);
int shm_size(int id);
int display_acquire(fih_display_t* out);
int name_register(const char* name);
int name_lookup(const char* name);
int map_region(Proc* p, uint32_t pages, uint32_t* va);

// ELF loader (elf.cpp)
int elf_load(uint32_t pd, File* f, uint32_t* entry, uint32_t* image_end, uint32_t* pages);

void syscall_dispatch(Regs* r);
