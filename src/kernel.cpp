#include "multiboot.hpp"
#include "graphics.hpp"
#include "drivers.hpp"
#include "vfs.hpp"
#include "gui.hpp"
#include "gdt.hpp"
#include "idt.hpp"
#include "timer.hpp"
#include "memory.hpp"
#include "task.hpp"
#include "syscall.hpp"
#include "io.hpp"

extern "C" uint32_t _kernel_end;

extern "C" void kernel_main(uint32_t magic, uint32_t addr) {
    if (magic != 0x2BADB002) return;

    auto* mbi = reinterpret_cast<multiboot_info*>(addr);
    uint32_t* vram = nullptr;
    uint32_t pitch_bytes = SCREEN_W * 4;

    if ((mbi->flags & (1 << 12)) && mbi->framebuffer_addr != 0) {
        vram = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(mbi->framebuffer_addr));
        pitch_bytes = mbi->framebuffer_pitch;
    } else if ((mbi->flags & (1 << 11)) && mbi->vbe_mode_info != 0) {
        uint8_t* vbe = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(mbi->vbe_mode_info));
        vram = reinterpret_cast<uint32_t*>(*reinterpret_cast<uint32_t*>(vbe + 40));
        pitch_bytes = *reinterpret_cast<uint16_t*>(vbe + 16);
    }

    if (!vram) return;

    // 1. Initial GDT & TSS
    gdt_init();

    // 2. IDT, PIC Remapping, ISRs 0..31, IRQs 0..15, Syscalls
    idt_init();

    // 3. PIT System Timer (100 Hz = 10ms per tick)
    timer_init(100);

    // 4. Memory Management (PMM & Kernel Heap)
    uint32_t mod_start = 0, mod_end = 0;
    if ((mbi->flags & (1 << 3)) && mbi->mods_count > 0) {
        auto* mod = reinterpret_cast<multiboot_module*>(mbi->mods_addr);
        mod_start = mod->mod_start;
        mod_end = mod->mod_end;
    }

    uint32_t kernel_end_addr = reinterpret_cast<uint32_t>(&_kernel_end);
    if (mod_end > kernel_end_addr) {
        kernel_end_addr = mod_end;
    }

    pmm_init(mbi, kernel_end_addr);
    // Allocate 16MB kernel heap at 32MB physical boundary
    kheap_init(0x02000000, 16 * 1024 * 1024);

    // 5. System Calls (int 0x80)
    syscall_init();

    // 6. Multitasking Scheduler
    task_init();

    // 7. Graphics & Hierarchical VFS
    gfx_init(vram, pitch_bytes);
    vfs_init(mod_start, mod_end);
    gui_init();

    // 8. Drivers: Mouse, Keyboard (via hardware IRQs)
    mouse_init();

    // 9. Enable CPU interrupts
    sti();

    bool prev_lclick = false;

    // Main OS Event & Desktop loop
    while (true) {
        blink_timer++;
        process_inputs();

        bool lclick_pressed = (!prev_lclick && mouse_btn_left);
        bool lclick_released = (prev_lclick && !mouse_btn_left);
        prev_lclick = mouse_btn_left;

        gui_handle_input(lclick_pressed, lclick_released);
        gui_draw();
        gfx_present();

        // Cooperative / Preemptive multitasking yield
        task_yield();
    }
}