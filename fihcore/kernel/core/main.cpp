#include "core/multiboot.hpp"
#include "arch/x86/cpu.hpp"
#include "dev/dev.hpp"
#include "fs/vfs.hpp"
#include "lib/lib.hpp"
#include "mm/heap.hpp"
#include "mm/pmm.hpp"
#include "mm/vmm.hpp"
#include "proc/proc.hpp"

extern "C" uint8_t _kernel_end[];
char g_cpu_name[48] = "unknown";

namespace {
char cmdline[256];
uint32_t initrd_start, initrd_end;
uint32_t boot_epoch;

void detect_cpu() {
    uint32_t a, b, c, d;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000));
    if (a < 0x80000004) {
        uint32_t v[3];
        asm volatile("cpuid" : "=a"(a), "=b"(v[0]), "=c"(v[2]), "=d"(v[1]) : "a"(0));
        memcpy(g_cpu_name, v, 12);
        g_cpu_name[12] = 0;
        return;
    }
    uint32_t* out = reinterpret_cast<uint32_t*>(g_cpu_name);
    for (uint32_t leaf = 0; leaf < 3; leaf++)
        asm volatile("cpuid" : "=a"(out[leaf * 4]), "=b"(out[leaf * 4 + 1]), "=c"(out[leaf * 4 + 2]),
                     "=d"(out[leaf * 4 + 3]) : "a"(0x80000002 + leaf));
    g_cpu_name[47] = 0;
    char* s = g_cpu_name;
    while (*s == ' ') s++;
    memmove(g_cpu_name, s, strlen(s) + 1);
}

// Returns the value of `key=` in the kernel command line, or `def`.
void cmdline_get(const char* key, char* out, size_t size, const char* def) {
    size_t klen = strlen(key);
    for (const char* p = cmdline; *p;) {
        while (*p == ' ') p++;
        const char* end = p;
        while (*end && *end != ' ') end++;
        if ((size_t)(end - p) > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            size_t n = kmin<size_t>((size_t)(end - p - klen - 1), size - 1);
            memcpy(out, p + klen + 1, n);
            out[n] = 0;
            return;
        }
        p = end;
    }
    strlcpy(out, def, size);
}
}

uint32_t clock_epoch() { return boot_epoch + timer_ticks() / 1000; }

extern "C" void kernel_main(uint32_t magic, const MultibootInfo* mbi) {
    serial_init();
    klog("Fihcore " FIHCORE_VERSION " starting\n");
    if (magic != MULTIBOOT_MAGIC) panic("not loaded by a Multiboot bootloader (magic %x)", magic);

    if ((mbi->flags & MB_INFO_CMDLINE) && mbi->cmdline)
        strlcpy(cmdline, reinterpret_cast<const char*>(mbi->cmdline), sizeof(cmdline));
    klog("cmdline: %s\n", cmdline);
    uint32_t reserved_end = (uint32_t)_kernel_end;
    if ((mbi->flags & MB_INFO_MODS) && mbi->mods_count) {
        auto* mod = reinterpret_cast<const MultibootModule*>(mbi->mods_addr);
        initrd_start = mod->start;
        initrd_end = mod->end;
        klog("initrd: %u KiB at %p\n", (initrd_end - initrd_start) / 1024, initrd_start);
    }
    if ((mbi->flags & MB_INFO_FRAMEBUFFER) && mbi->framebuffer_type == 1 && mbi->framebuffer_addr < 0x100000000ull) {
        g_fb.phys = (uint32_t)mbi->framebuffer_addr;
        g_fb.width = mbi->framebuffer_width;
        g_fb.height = mbi->framebuffer_height;
        g_fb.pitch = mbi->framebuffer_pitch;
        g_fb.bpp = mbi->framebuffer_bpp;
        klog("framebuffer: %ux%u %u bpp at %p\n", g_fb.width, g_fb.height, g_fb.bpp, g_fb.phys);
    } else {
        klog("framebuffer: none (text mode or unsupported format)\n");
    }

    gdt_init();
    idt_init();
    pmm_init(mbi, reserved_end);
    if (initrd_end > initrd_start) pmm_reserve(initrd_start, initrd_end);
    vmm_init();
    if (g_fb.phys) g_fb.virt = vmm_map_mmio(g_fb.phys, g_fb.pitch * g_fb.height);
    console_init();
    klog("Fihcore " FIHCORE_VERSION "\n");
    uint32_t heap = kmax<uint32_t>(4u << 20, kmin<uint32_t>(16u << 20, pmm_total_frames() * PAGE_SIZE / 8));
    heap_init(heap);
    detect_cpu();
    klog("cpu: %s\n", g_cpu_name);

    timer_init();
    proc_init();
    sti();
    ps2_init();
    ata_init();
    boot_epoch = rtc_epoch();

    vfs_init();
    if (initrd_end > initrd_start) {
        Fs* sys = tarfs_create(reinterpret_cast<const uint8_t*>(initrd_start), initrd_end - initrd_start);
        if (sys) vfs_mount("/system", sys);
    }
    char root[16];
    cmdline_get("root", root, sizeof(root), "auto");
    bool mounted = false;
    for (int i = 0; i < ATA_MAX && !mounted; i++) {
        if (strcmp(root, "auto") != 0 && (root[0] - '0') != i) continue;
        if (Fs* fs = fihfs_open_device(i)) {
            vfs_mount("/", fs);
            mounted = true;
        }
    }
    if (!mounted) klog("vfs: no FihFS data disk found; '/' is empty and read-only\n");

    char init[FIH_PATH_MAX];
    cmdline_get("init", init, sizeof(init), "/system/bin/init");
    File* con = vfs_console();
    File* stdio[3] = {con, con, con};
    const char* argv[1] = {init};
    int pid = proc_spawn(init, argv, 1, stdio);
    file_close(con);
    if (pid < 0) panic("cannot start init '%s' (error %d)", init, pid);
    sched_idle_loop();
}
