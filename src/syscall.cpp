#include "syscall.hpp"
#include "task.hpp"
#include "timer.hpp"
#include "vfs.hpp"
#include "io.hpp"

static void syscall_handler(registers_t* regs) {
    uint32_t num  = regs->eax;
    uint32_t arg1 = regs->ebx;
    uint32_t arg2 = regs->ecx;
    uint32_t arg3 = regs->edx;

    switch (num) {
        case SYS_YIELD:
            task_yield();
            regs->eax = 0;
            break;

        case SYS_EXIT:
            task_exit();
            regs->eax = 0;
            break;

        case SYS_SLEEP:
            task_sleep(arg1);
            regs->eax = 0;
            break;

        case SYS_GETPID: {
            task_t* cur = task_get_current();
            regs->eax = cur ? cur->id : 0;
            break;
        }

        case SYS_GETUPTIME:
            regs->eax = get_tick_count();
            break;

        case SYS_VFS_READ: {
            const char* path = reinterpret_cast<const char*>(arg1);
            vfs_node_t* node = vfs_lookup(path);
            if (node && node->type == FS_FILE) {
                char* buf = reinterpret_cast<char*>(arg2);
                uint32_t max_len = arg3;
                uint32_t to_copy = (node->size < max_len) ? node->size : max_len;
                for (uint32_t i = 0; i < to_copy; i++) buf[i] = node->content[i];
                buf[to_copy] = '\0';
                regs->eax = to_copy;
            } else {
                regs->eax = 0xFFFFFFFF; // Error
            }
            break;
        }

        case SYS_VFS_WRITE: {
            const char* path = reinterpret_cast<const char*>(arg1);
            const char* data = reinterpret_cast<const char*>(arg2);
            uint32_t sz = arg3;
            regs->eax = vfs_write_file(path, data, sz) ? 0 : 1;
            break;
        }

        case SYS_BEEP: {
            // Beep via PC Speaker
            uint32_t freq = arg1 ? arg1 : 1000;
            uint32_t div = 1193180 / freq;
            outb(0x43, 0xB6);
            outb(0x42, static_cast<uint8_t>(div & 0xFF));
            outb(0x42, static_cast<uint8_t>((div >> 8) & 0xFF));
            uint8_t tmp = inb(0x61);
            if (tmp != (tmp | 3)) {
                outb(0x61, tmp | 3);
            }
            sleep_ms(arg2 ? arg2 : 50);
            outb(0x61, inb(0x61) & 0xFC); // Turn off speaker
            regs->eax = 0;
            break;
        }

        default:
            regs->eax = 0xFFFFFFFF;
            break;
    }
}

void syscall_init() {
    register_interrupt_handler(128, syscall_handler);
}
