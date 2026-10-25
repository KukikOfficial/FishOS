// FishOS init: prepares the user's home on the data disk and keeps the
// desktop running.
#include <fihcore/syscall.h>
#include <stdio.h>
#include <string.h>

namespace {
const char WELCOME[] =
    "Welcome to FishOS!\n"
    "\n"
    "This file lives on your data disk, so changes you save survive a reboot.\n"
    "\n"
    "Try this:\n"
    "  - Open Files from the FishOS menu (or press Ctrl+Esc).\n"
    "  - Create a folder with Ctrl+Shift+N and a document with Ctrl+N.\n"
    "  - Double-click a document to edit it; Ctrl+S saves.\n"
    "  - Open Terminal and type 'help'.\n"
    "\n"
    "Shortcuts: Alt+Tab switches windows, Alt+F4 closes the active one.\n";

void ensure_home() {
    fih_stat_t st;
    if (fih_stat("/home", &st) == 0) return;
    int e = fih_mkdir("/home");
    if (e < 0) {
        printf("init: no writable data disk (%d); user files will not be saved\n", e);
        return;
    }
    fih_mkdir("/home/Documents");
    int fd = fih_open("/home/Welcome.txt", FIH_O_WRITE | FIH_O_CREATE | FIH_O_EXCL);
    if (fd >= 0) {
        fih_write(fd, WELCOME, sizeof(WELCOME) - 1);
        fih_close(fd);
    }
    printf("init: created /home on first boot\n");
}
}

int main() {
    printf("init: FishOS starting\n");
    ensure_home();
    fih_stat_t st;
    if (fih_stat("/home", &st) == 0) fih_chdir("/home");
    const char* argv[] = {"/system/bin/desktop", nullptr};
    int failures = 0;
    for (;;) {
        uint32_t started = fih_uptime();
        int pid = fih_spawn(argv[0], argv, nullptr);
        if (pid < 0) {
            printf("init: cannot start the desktop (%d)\n", pid);
            return 1;
        }
        int code = 0;
        while (fih_wait(pid, &code, 0) != pid) {}
        printf("init: desktop exited with %d\n", code);
        // Restart after crashes, but stop if it keeps failing right away.
        failures = fih_uptime() - started < 5000 ? failures + 1 : 0;
        if (failures >= 3) {
            printf("init: desktop failed repeatedly, giving up\n");
            return 1;
        }
        fih_sleep(500);
    }
}
