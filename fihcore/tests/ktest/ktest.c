/*
 * Fihcore integration test. Runs as init inside QEMU, exercises the system
 * call interface end to end and reports TAP-style lines on the kernel log.
 * The same binary acts as its own child process for spawn/IPC tests.
 */
#include <fihcore/syscall.h>
#include <stddef.h>

static int passed, failed;

static size_t slen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }
static int scmp(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a - *b; }
static void out(const char* s) { fih_write(1, s, slen(s)); }
static void out_int(int v) {
    char buf[16];
    int n = 0;
    unsigned u = v < 0 ? -(unsigned)v : (unsigned)v;
    do { buf[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) buf[n++] = '-';
    char rev[16];
    for (int i = 0; i < n; i++) rev[i] = buf[n - 1 - i];
    fih_write(1, rev, (uint32_t)n);
}

static void check(int cond, const char* what, int value) {
    if (cond) { passed++; out("ok "); }
    else { failed++; out("not ok "); }
    out_int(passed + failed);
    out(" - ");
    out(what);
    if (!cond) { out(" (got "); out_int(value); out(")"); }
    out("\n");
}
#define CHECK(c, v) check((c), #c, (int)(v))

static int write_file(const char* path, const char* text) {
    int fd = fih_open(path, FIH_O_WRITE | FIH_O_CREATE | FIH_O_TRUNC);
    if (fd < 0) return fd;
    int n = fih_write(fd, text, (uint32_t)slen(text));
    fih_close(fd);
    return n;
}

static int read_file(const char* path, char* buf, int max) {
    int fd = fih_open(path, FIH_O_READ);
    if (fd < 0) return fd;
    int n = fih_read(fd, buf, (uint32_t)max - 1);
    fih_close(fd);
    if (n >= 0) buf[n] = 0;
    return n;
}

static int recurse(int depth) {
    volatile char pad[1024];
    pad[0] = (char)depth;
    return depth ? recurse(depth - 1) + pad[0] - (char)depth : 0;
}

static int child(int argc, char** argv) {
    const char* mode = argv[1];
    if (!scmp(mode, "exit")) return 7;
    if (!scmp(mode, "crash")) { *(volatile int*)0 = 1; return 0; }
    if (!scmp(mode, "echo")) { out("hello from child"); return 0; }
    if (!scmp(mode, "sleep")) { fih_sleep(10000); return 0; }
    if (!scmp(mode, "shm") && argc >= 4) {
        int id = 0, denied = 0;
        for (const char* p = argv[2]; *p; p++) id = id * 10 + (*p - '0');
        for (const char* p = argv[3]; *p; p++) denied = denied * 10 + (*p - '0');
        if (fih_shm_map(denied) || fih_shm_size(denied) != -FIH_EPERM) return 2; // must not be allowed
        volatile uint32_t* mem = (volatile uint32_t*)fih_shm_map(id);
        if (!mem) return 3;
        mem[0] = mem[0] + 1000;
        return 0;
    }
    if (!scmp(mode, "fpu")) {
        volatile double x = 1.0;
        for (int i = 0; i < 200000; i++) { x = x * 1.000001; if ((i & 4095) == 0) fih_yield(); }
        return x > 1.2 && x < 1.3 ? 0 : 1;
    }
    return 99;
}

static void test_persist_second_boot(void) {
    char buf[128];
    int n = read_file("/ktest/persist.txt", buf, sizeof buf);
    if (n > 0 && !scmp(buf, "survives reboot\n")) {
        out("KTEST PERSIST: PASS\n");
        fih_unlink("/ktest/persist.txt");
        fih_unlink("/ktest");
    } else {
        out("KTEST PERSIST: FAIL\n");
    }
}

int main(int argc, char** argv) {
    if (argc > 1) return child(argc, argv);
    fih_stat_t st;
    if (fih_stat("/ktest/persist.txt", &st) == 0) {
        test_persist_second_boot();
        fih_power(FIH_POWER_SHUTDOWN);
        return 0;
    }
    out("ktest: starting\n");
    const char* self = "/system/bin/init";

    CHECK(fih_getpid() == 1, fih_getpid());

    // Memory: heap growth and demand-paged stack.
    uint32_t base = (uint32_t)fih_brk(0);
    CHECK(fih_brk(base + 3 * 4096 + 5) == (int)(base + 3 * 4096 + 5), 0);
    volatile uint8_t* heap = (volatile uint8_t*)base;
    heap[0] = 1; heap[3 * 4096 + 4] = 2;
    CHECK(heap[0] == 1 && heap[3 * 4096 + 4] == 2, 0);
    CHECK(fih_brk(base) == (int)base, 0);
    CHECK(fih_brk(0x10000000) == -FIH_ENOMEM, fih_brk(0x10000000));
    CHECK(recurse(1500) == 0, 0); // ~1.5 MiB of stack

    // Bad pointers are rejected, not followed.
    CHECK(fih_write(1, (void*)0x10, 4) == -FIH_EFAULT, fih_write(1, (void*)0x10, 4));
    CHECK(fih_read(0, (void*)0x100000, 4) == -FIH_EFAULT, fih_read(0, (void*)0x100000, 4));
    CHECK(fih_open((const char*)0xC0001000, FIH_O_READ) == -FIH_EFAULT, 0);
    CHECK(fih_close(17) == -FIH_EBADF, 0);

    // Filesystem on the data disk.
    char buf[256];
    CHECK(read_file("/seed.txt", buf, sizeof buf) == 15 && !scmp(buf, "seeded by host\n"), 0);
    int r = fih_mkdir("/ktest");
    CHECK(r == 0, r);
    CHECK(fih_mkdir("/ktest") == -FIH_EEXIST, 0);
    CHECK(write_file("/ktest/a.txt", "line one\nline two\n") == 18, 0);
    CHECK(read_file("/ktest/a.txt", buf, sizeof buf) == 18 && !scmp(buf, "line one\nline two\n"), 0);
    CHECK(fih_stat("/ktest/a.txt", &st) == 0 && st.size == 18 && st.type == FIH_FT_FILE, st.size);
    CHECK(fih_chdir("/ktest") == 0, 0);
    CHECK(fih_getcwd(buf, sizeof buf) == 6 && !scmp(buf, "/ktest"), 0);
    CHECK(read_file("a.txt", buf, sizeof buf) == 18, 0);
    CHECK(fih_rename("a.txt", "../ktest/b.txt", 0) == 0, 0);
    CHECK(fih_stat("a.txt", &st) == -FIH_ENOENT, 0);
    CHECK(write_file("c.txt", "new") == 3, 0);
    CHECK(fih_rename("c.txt", "b.txt", 0) == -FIH_EEXIST, 0);
    CHECK(fih_rename("c.txt", "b.txt", FIH_RENAME_REPLACE) == 0, 0);
    CHECK(read_file("b.txt", buf, sizeof buf) == 3 && !scmp(buf, "new"), 0);
    int fd = fih_open("b.txt", FIH_O_WRITE | FIH_O_APPEND);
    CHECK(fd >= 0 && fih_write(fd, "er", 2) == 2, fd);
    fih_close(fd);
    CHECK(read_file("b.txt", buf, sizeof buf) == 5 && !scmp(buf, "newer"), 0);
    fd = fih_open("/ktest", FIH_O_READ);
    fih_dirent_t de;
    CHECK(fd >= 0 && fih_readdir(fd, 0, &de) == 1 && !scmp(de.name, "b.txt") && de.size == 5, 0);
    CHECK(fih_readdir(fd, 1, &de) == 0, 0);
    fih_close(fd);
    CHECK(fih_unlink("/ktest") == -FIH_ENOTEMPTY, 0);
    CHECK(fih_unlink("/") == -FIH_EBUSY, 0);
    CHECK(fih_open("/ktest", FIH_O_WRITE) == -FIH_EISDIR, 0);
    CHECK(fih_unlink("b.txt") == 0, 0);
    fih_chdir("/");
    fih_statfs_t sf;
    CHECK(fih_statfs("/", &sf) == 0 && sf.free_blocks > 0 && !scmp(sf.fstype, "fihfs"), 0);

    // The boot image is mounted read-only.
    CHECK(fih_stat("/system/bin/init", &st) == 0 && st.size > 0, 0);
    CHECK(fih_open("/system/bin/init", FIH_O_WRITE) == -FIH_EROFS, 0);
    CHECK(fih_unlink("/system") == -FIH_EBUSY, 0);
    fd = fih_open("/", FIH_O_READ);
    int saw_system = 0;
    for (uint32_t i = 0; fih_readdir(fd, i, &de) == 1; i++) saw_system |= !scmp(de.name, "system");
    fih_close(fd);
    CHECK(saw_system, 0);

    // Processes.
    const char* a_exit[] = {self, "exit", 0};
    int code = 0, pid = fih_spawn(self, a_exit, 0);
    CHECK(pid > 1, pid);
    CHECK(fih_wait(pid, &code, 0) == pid && code == 7, code);
    CHECK(fih_wait(pid, &code, 0) == -FIH_ECHILD, 0);
    const char* a_crash[] = {self, "crash", 0};
    pid = fih_spawn(self, a_crash, 0);
    CHECK(fih_wait(pid, &code, 0) == pid && code == -142, code);
    CHECK(fih_spawn("/system/bin/nothing", 0, 0) == -FIH_ENOENT, 0);
    CHECK(fih_spawn("/seed.txt", 0, 0) == -FIH_ENOEXEC, fih_spawn("/seed.txt", 0, 0));
    const char* a_sleep[] = {self, "sleep", 0};
    pid = fih_spawn(self, a_sleep, 0);
    fih_sleep(20);
    CHECK(fih_kill(pid) == 0, 0);
    CHECK(fih_wait(pid, &code, 0) == pid && code == -9, code);
    fih_msg_t m;
    int exits = 0;
    while (fih_msg_recv(&m, 0) == 1) exits += m.type == FIH_MSG_CHILD_EXIT;
    CHECK(exits == 3, exits);

    // Pipes.
    int p[2];
    CHECK(fih_pipe(p, 0) == 0, 0);
    const char* a_echo[] = {self, "echo", 0};
    int fds[3] = {-1, p[1], p[1]};
    pid = fih_spawn(self, a_echo, fds);
    fih_close(p[1]);
    int got = 0, n;
    while ((n = fih_read(p[0], buf + got, sizeof buf - 1 - got)) > 0) got += n;
    buf[got] = 0;
    CHECK(!scmp(buf, "hello from child"), got);
    fih_close(p[0]);
    fih_wait(pid, &code, 0);
    CHECK(fih_pipe(p, FIH_O_NONBLOCK) == 0 && fih_read(p[0], buf, 4) == -FIH_EAGAIN, 0);
    fih_close(p[0]);
    CHECK(fih_write(p[1], "x", 1) == -FIH_EPIPE, 0);
    fih_close(p[1]);

    // Messages.
    while (fih_msg_recv(&m, 0) == 1) {} // drop child-exit notifications
    m.type = FIH_MSG_USER + 1;
    m.a[0] = 42;
    CHECK(fih_msg_send(fih_getpid(), &m) == 0, 0);
    CHECK(fih_msg_recv(&m, 100) == 1 && m.a[0] == 42 && m.from == 1, 0);
    m.type = FIH_MSG_KEY;
    CHECK(fih_msg_send(fih_getpid(), &m) == -FIH_EINVAL, 0);
    uint32_t t0 = fih_uptime();
    CHECK(fih_msg_recv(&m, 50) == 0 && fih_uptime() - t0 >= 45, fih_uptime() - t0);
    CHECK(fih_name_register("ktest.service") == 0 && fih_name_lookup("ktest.service") == 1, 0);

    // Shared memory with explicit grants.
    int id = fih_shm_create(8192), denied = fih_shm_create(4096);
    volatile uint32_t* shared = (volatile uint32_t*)fih_shm_map(id);
    CHECK(id > 0 && shared != 0, id);
    CHECK(fih_shm_size(id) == 8192 && fih_shm_size(9999) == -FIH_ENOENT, fih_shm_size(id));
    shared[0] = 5;
    CHECK(fih_shm_grant(id, 0) == 0, 0);
    char idbuf[12], dbuf[12];
    int k = 0, v = id;
    char tmp[12];
    do { tmp[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (int i = 0; i < k; i++) idbuf[i] = tmp[k - 1 - i];
    idbuf[k] = 0;
    k = 0; v = denied;
    do { tmp[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (int i = 0; i < k; i++) dbuf[i] = tmp[k - 1 - i];
    dbuf[k] = 0;
    const char* a_shm[] = {self, "shm", idbuf, dbuf, 0};
    // Grant to the pid the child will receive: spawn first, then grant before it runs.
    pid = fih_spawn(self, a_shm, 0);
    fih_shm_grant(id, pid);
    CHECK(fih_wait(pid, &code, 0) == pid && code == 0, code);
    CHECK(shared[0] == 1005, shared[0]);
    CHECK(fih_shm_unmap(id) == 0 && fih_shm_destroy(id) == 0 && fih_shm_destroy(denied) == 0, 0);

    // x87 state is preserved across context switches.
    const char* a_fpu[] = {self, "fpu", 0};
    int pa = fih_spawn(self, a_fpu, 0), pb = fih_spawn(self, a_fpu, 0);
    volatile double y = 2.0;
    for (int i = 0; i < 1000; i++) { y = y * 1.5 / 1.5; if ((i & 63) == 0) fih_yield(); }
    int ca = -1, cb = -1;
    fih_wait(pa, &ca, 0);
    fih_wait(pb, &cb, 0);
    CHECK(ca == 0 && cb == 0 && y == 2.0, ca * 10 + cb);

    fih_sysinfo_t si;
    CHECK(fih_sysinfo(&si) == 0 && si.mem_total_kb > 64 * 1024 && si.nprocs >= 1, si.mem_total_kb);
    fih_procinfo_t procs[8];
    CHECK(fih_proc_list(procs, 8) >= 1 && procs[0].pid == 1, 0);
    fih_time_t tm;
    CHECK(fih_time(&tm) == 0 && tm.year >= 2024, tm.year);

    while (fih_msg_recv(&m, 0) == 1) {}
    fih_mkdir("/ktest");
    write_file("/ktest/persist.txt", "survives reboot\n");

    out(failed ? "KTEST RESULT: FAIL " : "KTEST RESULT: PASS ");
    out_int(passed);
    out("/");
    out_int(passed + failed);
    out("\n");
    fih_power(FIH_POWER_SHUTDOWN);
    return failed;
}
