#include "terminal.hpp"
#include "graphics.hpp"
#include "vfs.hpp"
#include "task.hpp"
#include "memory.hpp"
#include "timer.hpp"
#include "drivers.hpp"
#include "string.hpp"
#include "io.hpp"
#include "pe.hpp"
#include "theme.hpp"
#include "gui.hpp"

constexpr int TERM_ROWS = 22;
constexpr int TERM_COLS = 58;

struct TermLine {
    char text[TERM_COLS + 1];
    uint32_t color;
};

static TermLine term_lines[TERM_ROWS];
static int term_count = 0;

static char input_buf[TERM_COLS] = "";
static int input_pos = 0;
static char cwd[64] = "/";

static void term_scroll() {
    for (int i = 0; i < TERM_ROWS - 1; i++) {
        term_lines[i] = term_lines[i + 1];
    }
    term_lines[TERM_ROWS - 1].text[0] = '\0';
    term_lines[TERM_ROWS - 1].color = 0xFFFFFF;
}

void terminal_print(const char* text, uint32_t color) {
    while (*text) {
        if (term_count >= TERM_ROWS) {
            term_scroll();
            term_count = TERM_ROWS - 1;
        }

        int cur_len = kstrlen(term_lines[term_count].text);
        if (*text == '\n') {
            term_count++;
            text++;
            continue;
        }

        if (cur_len < TERM_COLS - 1) {
            term_lines[term_count].text[cur_len] = *text;
            term_lines[term_count].text[cur_len + 1] = '\0';
            term_lines[term_count].color = color;
        } else {
            term_count++;
            if (term_count >= TERM_ROWS) {
                term_scroll();
                term_count = TERM_ROWS - 1;
            }
            term_lines[term_count].text[0] = *text;
            term_lines[term_count].text[1] = '\0';
            term_lines[term_count].color = color;
        }
        text++;
    }
}

void terminal_println(const char* text, uint32_t color) {
    terminal_print(text, color);
    if (term_count < TERM_ROWS) term_count++;
    if (term_count >= TERM_ROWS) {
        term_scroll();
        term_count = TERM_ROWS - 1;
    }
}

static void term_clear() {
    for (int i = 0; i < TERM_ROWS; i++) {
        term_lines[i].text[0] = '\0';
        term_lines[i].color = 0xFFFFFF;
    }
    term_count = 0;
}

static void execute_command(const char* cmd) {
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') return;

    // 1. help
    if (kstrcmp(cmd, "help")) {
        terminal_println("=== FishShell v2.0 Command Help ===", 0x38BDF8);
        terminal_println("wine <file.exe>   Run Windows PE executable", 0xFBBF24);
        terminal_println("ls [path]         List directory contents", 0xFFFFFF);
        terminal_println("cd <path>         Change current directory", 0xFFFFFF);
        terminal_println("pwd               Print working directory", 0xFFFFFF);
        terminal_println("cat <file>        View text file contents", 0xFFFFFF);
        terminal_println("echo <txt> [> f]  Print or write text to file", 0xFFFFFF);
        terminal_println("mkdir <dir>       Create directory", 0xFFFFFF);
        terminal_println("rm <path>         Delete file or directory", 0xFFFFFF);
        terminal_println("ps                List active system tasks", 0xFFFFFF);
        terminal_println("free              Show RAM memory stats", 0xFFFFFF);
        terminal_println("uptime            Show system uptime", 0xFFFFFF);
        terminal_println("beep [f] [ms]     Play sound tone", 0xFFFFFF);
        terminal_println("theme <id>        Switch theme (dark/win95/matrix)", 0xFFFFFF);
        terminal_println("snake             Launch Snake arcade game", 0x4ADE80);
        terminal_println("clear / reboot    Clear screen or reboot PC", 0xFFFFFF);
    }
    // 2. wine <file.exe>
    else if (cmd[0] == 'w' && cmd[1] == 'i' && cmd[2] == 'n' && cmd[3] == 'e' && (cmd[4] == ' ' || cmd[4] == '\0')) {
        const char* path = cmd + 4;
        while (*path == ' ') path++;
        if (*path == '\0') {
            terminal_println("Usage: wine <file.exe>", 0xF87171);
            return;
        }

        terminal_print("FishWine: Loading Win32 PE binary '", 0xFBBF24);
        terminal_print(path, 0xFFFFFF);
        terminal_println("'...", 0xFBBF24);

        if (wine_load_pe(path)) {
            terminal_println("[FishWine] PE32 loaded and executed!", 0x4ADE80);
        } else {
            terminal_println("[FishWine Error] Invalid or missing PE .exe file", 0xEF4444);
        }
    }
    // 3. ls
    else if (cmd[0] == 'l' && cmd[1] == 's' && (cmd[2] == ' ' || cmd[2] == '\0')) {
        const char* target_path = (cmd[2] == ' ') ? cmd + 3 : cwd;
        while (*target_path == ' ') target_path++;
        if (*target_path == '\0') target_path = cwd;

        vfs_node_t* dir = vfs_lookup(target_path);
        if (!dir || dir->type != FS_DIRECTORY) {
            terminal_println("ls: directory not found", 0xEF4444);
            return;
        }

        vfs_node_t* child = dir->first_child;
        if (!child) {
            terminal_println("(empty directory)", 0x64748B);
            return;
        }

        while (child) {
            if (child->type == FS_DIRECTORY) {
                terminal_print("[DIR]  ", 0xF59E0B);
                terminal_println(child->name, 0xFBBF24);
            } else {
                terminal_print("       ", 0x94A3B8);
                terminal_print(child->name, 0xFFFFFF);
                char sz_buf[16];
                int_to_str(child->size, sz_buf);
                terminal_print(" (", 0x64748B);
                terminal_print(sz_buf, 0x64748B);
                terminal_println(" B)", 0x64748B);
            }
            child = child->next_sibling;
        }
    }
    // 4. pwd
    else if (kstrcmp(cmd, "pwd")) {
        terminal_println(cwd, 0x38BDF8);
    }
    // 5. cd
    else if (cmd[0] == 'c' && cmd[1] == 'd' && (cmd[2] == ' ' || cmd[2] == '\0')) {
        const char* path = cmd + 2;
        while (*path == ' ') path++;
        if (*path == '\0' || kstrcmp(path, "/")) {
            kstrcpy(cwd, "/");
            vfs_set_current_dir(vfs_get_root());
        } else if (kstrcmp(path, "..")) {
            vfs_node_t* cur = vfs_get_current_dir();
            if (cur && cur->parent) {
                vfs_set_current_dir(cur->parent);
                if (cur->parent == vfs_get_root()) kstrcpy(cwd, "/");
                else kstrcpy(cwd, cur->parent->name);
            }
        } else {
            vfs_node_t* n = vfs_lookup(path);
            if (n && n->type == FS_DIRECTORY) {
                vfs_set_current_dir(n);
                kstrcpy(cwd, n->name);
            } else {
                terminal_println("cd: path not found", 0xEF4444);
            }
        }
    }
    // 6. cat
    else if (cmd[0] == 'c' && cmd[1] == 'a' && cmd[2] == 't' && cmd[3] == ' ') {
        const char* path = cmd + 4;
        while (*path == ' ') path++;
        vfs_node_t* f = vfs_lookup(path);
        if (f && f->type == FS_FILE && f->content) {
            terminal_println(f->content, 0xE2E8F0);
        } else {
            terminal_println("cat: file not found", 0xEF4444);
        }
    }
    // 7. mkdir
    else if (cmd[0] == 'm' && cmd[1] == 'k' && cmd[2] == 'd' && cmd[3] == 'i' && cmd[4] == 'r' && cmd[5] == ' ') {
        const char* path = cmd + 6;
        while (*path == ' ') path++;
        if (vfs_mkdir(path)) {
            terminal_println("Directory created.", 0x4ADE80);
        } else {
            terminal_println("mkdir: failed to create", 0xEF4444);
        }
    }
    // 8. rm
    else if (cmd[0] == 'r' && cmd[1] == 'm' && cmd[2] == ' ') {
        const char* path = cmd + 3;
        while (*path == ' ') path++;
        vfs_node_t* n = vfs_lookup(path);
        if (n && vfs_delete_node(n)) {
            terminal_println("Item deleted.", 0x4ADE80);
        } else {
            terminal_println("rm: cannot remove path", 0xEF4444);
        }
    }
    // 9. ps
    else if (kstrcmp(cmd, "ps")) {
        terminal_println("PID  NAME                 STATE     RING", 0x64748B);
        task_t* cur_task = task_get_list();
        if (cur_task) {
            task_t* t = cur_task;
            do {
                char line[64];
                char pid_buf[8];
                int_to_str(t->id, pid_buf);
                int p = 0;
                for (int s = 0; pid_buf[s]; s++) line[p++] = pid_buf[s];
                while (p < 5) line[p++] = ' ';
                for (int s = 0; t->name[s] && s < 20; s++) line[p++] = t->name[s];
                while (p < 26) line[p++] = ' ';
                const char* st = (t->state == TASK_RUNNING) ? "RUN" : "WAIT";
                for (int s = 0; st[s]; s++) line[p++] = st[s];
                while (p < 36) line[p++] = ' ';
                const char* r = t->is_user ? "Ring 3" : "Ring 0";
                for (int s = 0; r[s]; s++) line[p++] = r[s];
                line[p] = '\0';
                terminal_println(line, t->is_user ? 0xC084FC : 0x38BDF8);
                t = t->next;
            } while (t && t != cur_task);
        }
    }
    // 10. free
    else if (kstrcmp(cmd, "free")) {
        char buf[64] = "Total RAM: ";
        char num[16];
        int_to_str(pmm_get_total_memory() / (1024 * 1024), num);
        int p = kstrlen(buf);
        for (int s = 0; num[s]; s++) buf[p++] = num[s];
        const char* free_str = " MB | Free: ";
        for (int s = 0; free_str[s]; s++) buf[p++] = free_str[s];
        int_to_str(pmm_get_free_memory() / (1024 * 1024), num);
        for (int s = 0; num[s]; s++) buf[p++] = num[s];
        buf[p++] = ' '; buf[p++] = 'M'; buf[p++] = 'B'; buf[p] = '\0';
        terminal_println(buf, 0x4ADE80);
    }
    // 11. uptime
    else if (kstrcmp(cmd, "uptime")) {
        char buf[32] = "Uptime: ";
        char num[16];
        int_to_str(get_tick_count() / 100, num);
        int p = kstrlen(buf);
        for (int s = 0; num[s]; s++) buf[p++] = num[s];
        buf[p++] = ' '; buf[p++] = 's'; buf[p++] = 'e'; buf[p++] = 'c'; buf[p] = '\0';
        terminal_println(buf, 0x38BDF8);
    }
    // 12. beep
    else if (cmd[0] == 'b' && cmd[1] == 'e' && cmd[2] == 'e' && cmd[3] == 'p') {
        audio_beep(1000, 100);
        terminal_println("Beep!", 0xFBBF24);
    }
    // 13. theme <dark|win95|matrix>
    else if (cmd[0] == 't' && cmd[1] == 'h' && cmd[2] == 'e' && cmd[3] == 'm' && cmd[4] == 'e' && cmd[5] == ' ') {
        const char* t = cmd + 6;
        while (*t == ' ') t++;
        if (kstrcmp(t, "win95") || kstrcmp(t, "classic")) {
            theme_set(THEME_WIN95);
            terminal_println("Theme switched to Classic Windows 95.", 0xFFFFFF);
        } else if (kstrcmp(t, "matrix")) {
            theme_set(THEME_MATRIX);
            terminal_println("Theme switched to Matrix.", 0x22C55E);
        } else {
            theme_set(THEME_DARK);
            terminal_println("Theme switched to Modern Dark.", 0x38BDF8);
        }
    }
    // 14. snake
    else if (kstrcmp(cmd, "snake")) {
        windows[5].closed = false;
        windows[5].minimized = false;
        terminal_println("Launching Snake arcade game...", 0x4ADE80);
    }
    // 15. clear
    else if (kstrcmp(cmd, "clear")) {
        term_clear();
    }
    // 16. reboot
    else if (kstrcmp(cmd, "reboot")) {
        terminal_println("Rebooting system...", 0xEF4444);
        outb(0x64, 0xFE);
    } else {
        terminal_print("Unknown command: '", 0xEF4444);
        terminal_print(cmd, 0xFFFFFF);
        terminal_println("'. Type 'help'.", 0xEF4444);
    }
}

void terminal_init() {
    term_clear();
    terminal_println("FishOS Terminal [FishShell v2.0 with FishWine]", 0x38BDF8);
    terminal_println("Type 'help' for commands or 'wine hello.exe' to run PE.", 0x94A3B8);
    terminal_println("", 0xFFFFFF);
}

void terminal_handle_char(char c) {
    if (c == '\n') {
        terminal_handle_enter();
        return;
    }
    if (input_pos < TERM_COLS - 2) {
        input_buf[input_pos++] = c;
        input_buf[input_pos] = '\0';
    }
}

void terminal_handle_backspace() {
    if (input_pos > 0) {
        input_buf[--input_pos] = '\0';
    }
}

void terminal_handle_enter() {
    // Print executed line
    terminal_print("fishos:", 0x38BDF8);
    terminal_print(cwd, 0xFBBF24);
    terminal_print("$ ", 0xFFFFFF);
    terminal_println(input_buf, 0xFFFFFF);

    // Run command
    execute_command(input_buf);

    input_buf[0] = '\0';
    input_pos = 0;
}

void terminal_draw(int x, int y, int w, int h) {
    fill_rect(x, y, w, h, 0x0A0F1D);
    draw_rect(x, y, w, h, 0x1E293B);

    int line_y = y + 8;
    for (int i = 0; i < TERM_ROWS; i++) {
        if (term_lines[i].text[0] != '\0') {
            draw_string(x + 8, line_y, term_lines[i].text, term_lines[i].color);
        }
        line_y += 12;
    }

    // Input prompt line at bottom
    int prompt_y = y + h - 18;
    fill_rect(x + 2, prompt_y - 2, w - 4, 16, 0x0F172A);
    draw_rect(x + 2, prompt_y - 2, w - 4, 16, 0x334155);

    draw_string(x + 6, prompt_y, "fishos$", 0x38BDF8);
    draw_string(x + 66, prompt_y, input_buf, 0xFFFFFF);

    // Blinking cursor
    if (blink_timer % 40 < 20) {
        int cur_x = x + 66 + input_pos * 8;
        fill_rect(cur_x, prompt_y + 1, 8, 10, 0x38BDF8);
    }
}
