// FishOS Terminal: a text console with a built-in shell. Commands that are
// not built in run as separate processes with their output piped back here.
#include <fihcore/syscall.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fishos/fs.h"
#include "fishos/ui.h"

using gfx::font_mono;
using ui::Event;

namespace {
constexpr int CW = 8, LH = 17, PAD = 8;
constexpr int MAX_COLS = 160;
constexpr int SCROLLBACK = 1000;
constexpr uint32_t BG = 0x13232B;
enum Color : uint8_t { C_TEXT, C_DIM, C_PROMPT, C_ERROR, C_DIR, C_OK };
const uint32_t PALETTE[] = {0xD7E3E8, 0x7F98A3, 0x6FD3B4, 0xF08C6C, 0x7CC4E0, 0xA8E39A};

struct Row {
    char ch[MAX_COLS];
    uint8_t color[MAX_COLS];
    int len;
};

ui::Window win;
Row* rows;            // ring buffer
int first_row, nrows; // nrows <= SCROLLBACK
int out_col;
int view_offset;      // rows scrolled back from the bottom
Color cur_color = C_TEXT;

char line[256];
int line_len, line_cursor;
char history[32][256];
int nhist, hist_pos = -1;

// Foreground external process.
int child_pid = -1, child_out = -1, child_in = -1;
uint32_t child_started;

// Redirection target for built-ins (`cmd > file`).
char* capture;
int capture_len, capture_cap;

int cols() {
    int c = (win.w - 2 * PAD - 10) / CW;
    return c < 10 ? 10 : (c > MAX_COLS ? MAX_COLS : c);
}
int visible_rows() { return (win.h - 2 * PAD) / LH; }
Row& row_at(int i) { return rows[(first_row + i) % SCROLLBACK]; }

void new_row() {
    if (nrows < SCROLLBACK) nrows++;
    else first_row = (first_row + 1) % SCROLLBACK;
    row_at(nrows - 1).len = 0;
    out_col = 0;
}

void put_raw(char c, Color color) {
    if (nrows == 0) new_row();
    if (c == '\n') { new_row(); return; }
    if (c == '\r') { out_col = 0; return; }
    if (c == '\b') { if (out_col > 0) out_col--; return; }
    if (c == '\t') { do put_raw(' ', color); while (out_col % 4); return; }
    if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '?';
    if (out_col >= cols()) new_row();
    Row& r = row_at(nrows - 1);
    r.ch[out_col] = c;
    r.color[out_col] = color;
    out_col++;
    if (out_col > r.len) r.len = out_col;
}

void out(const char* s, int n = -1, Color color = C_TEXT) {
    if (n < 0) n = (int)strlen(s);
    if (capture) {
        if (capture_len + n + 1 > capture_cap) {
            int cap = (capture_len + n + 1) * 2;
            char* grown = static_cast<char*>(realloc(capture, (size_t)cap));
            if (!grown) return;
            capture = grown;
            capture_cap = cap;
        }
        memcpy(capture + capture_len, s, (size_t)n);
        capture_len += n;
        return;
    }
    for (int i = 0; i < n; i++) {
        // Skip ANSI escape sequences (ESC [ params letter).
        if (s[i] == 0x1B && i + 1 < n && s[i + 1] == '[') {
            i += 2;
            while (i < n && !((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z'))) i++;
            continue;
        }
        put_raw(s[i], color);
    }
}


void outf(Color color, const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    out(buf, n < (int)sizeof buf ? n : (int)sizeof buf - 1, color);
}

void err(const char* what, int e) { outf(C_ERROR, "%s: %s\n", what, ui::error_string(e)); }

void prompt() {
    if (out_col) out("\n");
    char cwd[FIH_PATH_MAX];
    fih_getcwd(cwd, sizeof cwd);
    out(cwd, -1, C_PROMPT);
    out(" $ ", -1, C_DIM);
}

void draw() {
    gfx::Canvas& c = win.canvas;
    c.clip = {0, 0, win.w, win.h};
    c.fill({0, 0, win.w, win.h}, BG);
    int vis = visible_rows();
    // The input line is drawn after the last output row when no child runs.
    bool show_input = child_pid < 0;
    int total = nrows;
    int last = total - 1 - view_offset;
    int first = last - vis + 1;
    for (int i = first; i <= last; i++) {
        if (i < 0) continue;
        Row& r = row_at(i);
        int y = PAD + (i - first) * LH;
        for (int k = 0; k < r.len; k++) {
            int run = k;
            while (run < r.len && r.color[run] == r.color[k]) run++;
            c.text(PAD + k * CW, y, r.ch + k, PALETTE[r.color[k]], font_mono, 0, run - k);
            k = run - 1;
        }
        if (i == total - 1 && view_offset == 0) {
            int x = PAD + out_col * CW;
            if (show_input) c.text(x, y, line, PALETTE[C_TEXT], font_mono, 0, line_len);
            int cx = x + (show_input ? line_cursor : line_len) * CW;
            if (win.focused) c.fill({cx, y, CW, LH - 1}, 0x6FD3B4);
            else c.frame({cx, y, CW, LH - 1}, 0x6FD3B4);
            if (show_input && line_cursor < line_len) c.text(cx, y, line + line_cursor, BG, font_mono, 0, 1);
            if (!show_input && child_in >= 0) c.text(x, y, line, PALETTE[C_TEXT], font_mono, 0, line_len);
        }
    }
    if (nrows > vis) { // scroll position indicator
        int h = win.h - 2 * PAD;
        int th = h * vis / nrows;
        if (th < 20) th = 20;
        int ty = PAD + (h - th) * (nrows - vis - view_offset) / (nrows - vis);
        c.fill_rounded({win.w - 8, ty, 4, th}, 2, view_offset ? 0x6FD3B4 : 0x3A535E);
    }
    win.present();
}

// ---- argument parsing ------------------------------------------------------
int split(char* s, char** argv, int max) {
    int argc = 0;
    while (*s && argc < max - 1) {
        while (*s == ' ') s++;
        if (!*s) break;
        char* dst = s;
        argv[argc++] = dst;
        char quote = 0;
        while (*s && (quote || *s != ' ')) {
            if ((*s == '"' || *s == '\'') && (!quote || quote == *s)) { quote = quote ? 0 : *s; s++; continue; }
            *dst++ = *s++;
        }
        if (*s) s++;
        *dst = 0;
    }
    argv[argc] = nullptr;
    return argc;
}

// ---- built-in commands -----------------------------------------------------
void list_dir(const char* path, bool lng) {
    fih_stat_t st;
    int e = fih_stat(path, &st);
    if (e) return err(path, e);
    if (st.type != FIH_FT_DIR) {
        outf(C_TEXT, "%s\n", path);
        return;
    }
    int fd = fih_open(path, FIH_O_READ);
    if (fd < 0) return err(path, fd);
    fih_dirent_t d;
    int col = 0;
    for (uint32_t i = 0; fih_readdir(fd, i, &d) == 1; i++) {
        bool dir = d.type == FIH_FT_DIR;
        if (lng) {
            char sz[24];
            if (dir) strlcpy(sz, "-", sizeof sz);
            else ui::format_size(sz, sizeof sz, d.size);
            outf(C_DIM, "%s %10s  ", dir ? "d" : "-", sz);
            outf(dir ? C_DIR : C_TEXT, "%s%s\n", d.name, dir ? "/" : "");
        } else {
            int w = (int)strlen(d.name) + (dir ? 1 : 0);
            int cell = w < 20 ? 20 : w + 2;
            if (col && col + cell > cols()) { out("\n"); col = 0; }
            outf(dir ? C_DIR : C_TEXT, "%s%s", d.name, dir ? "/" : "");
            for (int k = w; k < cell; k++) out(" ");
            col += cell;
        }
    }
    if (col) out("\n");
    fih_close(fd);
}

void cmd_cat(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        int fd = fih_open(argv[i], FIH_O_READ);
        if (fd < 0) { err(argv[i], fd); continue; }
        char buf[1024];
        int n;
        while ((n = fih_read(fd, buf, sizeof buf)) > 0) out(buf, n);
        if (n < 0) err(argv[i], n);
        fih_close(fd);
    }
}

void cmd_sysinfo() {
    fih_sysinfo_t si;
    if (fih_sysinfo(&si) < 0) return;
    uint32_t up = si.uptime_ms / 1000;
    outf(C_PROMPT, "FishOS");
    outf(C_TEXT, " on %s (ABI %u)\n", si.kernel, si.abi_version);
    outf(C_TEXT, "CPU:       %s\n", si.cpu);
    outf(C_TEXT, "Memory:    %u MiB used of %u MiB\n", (si.mem_total_kb - si.mem_free_kb) / 1024, si.mem_total_kb / 1024);
    outf(C_TEXT, "Kernel heap: %u KiB used of %u KiB\n", si.heap_used_kb, si.heap_total_kb);
    outf(C_TEXT, "Uptime:    %uh %02um %02us\n", up / 3600, (up / 60) % 60, up % 60);
    outf(C_TEXT, "Processes: %u\n", si.nprocs);
}

void cmd_df() {
    const char* mounts[] = {"/", "/system"};
    outf(C_DIM, "%-10s %-7s %-8s %10s %10s\n", "Mount", "Type", "Device", "Size", "Free");
    for (const char* m : mounts) {
        fih_statfs_t sf;
        if (fih_statfs(m, &sf) < 0) continue;
        char size[24] = "-", free_s[24] = "-";
        if (sf.block_size && sf.total_blocks) {
            ui::format_size(size, sizeof size, sf.total_blocks * sf.block_size);
            ui::format_size(free_s, sizeof free_s, sf.free_blocks * sf.block_size);
        }
        outf(C_TEXT, "%-10s %-7s %-8s %10s %10s%s\n", m, sf.fstype, sf.device, size, free_s,
             (sf.flags & FIH_ST_READONLY) ? "  read-only" : "");
    }
}

void cmd_ps() {
    fih_procinfo_t p[64];
    int n = fih_proc_list(p, 64);
    static const char* const states[] = {"?", "ready", "running", "waiting", "zombie"};
    outf(C_DIM, "%5s %5s %-8s %8s %8s  %s\n", "PID", "PPID", "STATE", "MEM", "CPU", "NAME");
    for (int i = 0; i < n; i++)
        outf(C_TEXT, "%5u %5u %-8s %6u K %6u ms  %s\n", p[i].pid, p[i].ppid, states[p[i].state < 5 ? p[i].state : 0],
             p[i].mem_kb, p[i].cpu_ms, p[i].name);
}

void cmd_help() {
    out("Built-in commands:\n", -1, C_PROMPT);
    const char* const help[] = {
        "ls [-l] [path]     list a folder",       "cd [path]          change folder (default /home)",
        "pwd                print current folder", "mkdir path...      create folders",
        "touch file...      create empty files",  "cat file...        print files",
        "rm [-r] path...    delete (-r: folders)", "mv from to         move or rename",
        "cp [-r] from to    copy (-r: folders)",   "echo text [> file] print or write text",
        "edit [file]        open the text editor", "open path          open with the right app",
        "sysinfo, ps, df    system information",  "kill pid           stop a process",
        "date, uptime       time information",    "clear              clear the screen",
        "reboot, shutdown   power control",       "exit               close the terminal",
    };
    for (const char* h : help) outf(C_TEXT, "  %s\n", h);
    out("Other commands run programs from /system/bin. Ctrl+C stops a running program.\n", -1, C_DIM);
    out("Shift+PgUp/PgDn or the mouse wheel scroll; Ctrl+Shift+V pastes.\n", -1, C_DIM);
}

bool is_gui_program(const char* path) {
    int dir = fih_open("/system/apps", FIH_O_READ);
    if (dir < 0) return false;
    fih_dirent_t d;
    bool found = false;
    for (uint32_t i = 0; !found && fih_readdir(dir, i, &d) == 1; i++) {
        char p[FIH_PATH_MAX], *data;
        uint32_t len;
        fs::join("/system/apps", d.name, p, sizeof p);
        if (fs::read_all(p, &data, &len, 4096) != 0) continue;
        char* exec = strstr(data, "exec=");
        if (exec) {
            exec += 5;
            char* end = strchr(exec, '\n');
            if (end) *end = 0;
            found = !strcmp(exec, path);
        }
        free(data);
    }
    fih_close(dir);
    return found;
}

int resolve_program(const char* name, char* out_path, size_t n) {
    if (strchr(name, '/')) return fs::absolute(name, out_path, n);
    snprintf(out_path, n, "/system/bin/%s", name);
    return 0;
}

void start_external(int argc, char** argv) {
    char path[FIH_PATH_MAX];
    int e = resolve_program(argv[0], path, sizeof path);
    fih_stat_t st;
    if (!e) e = fih_stat(path, &st);
    if (e == -FIH_ENOENT) { outf(C_ERROR, "%s: command not found (try 'help')\n", argv[0]); return; }
    if (e) return err(argv[0], e);
    if (is_gui_program(path)) { // windowed programs run alongside the terminal
        int pid = fih_spawn(path, const_cast<const char* const*>(argv), nullptr);
        if (pid < 0) err(argv[0], pid);
        else outf(C_DIM, "started %s (pid %d)\n", argv[0], pid);
        return;
    }
    int out_pipe[2], in_pipe[2];
    if ((e = fih_pipe(out_pipe, FIH_O_NONBLOCK)) < 0) return err("pipe", e);
    if ((e = fih_pipe(in_pipe, 0)) < 0) {
        fih_close(out_pipe[0]);
        fih_close(out_pipe[1]);
        return err("pipe", e);
    }
    int fds[3] = {in_pipe[0], out_pipe[1], out_pipe[1]};
    int pid = fih_spawn(path, const_cast<const char* const*>(argv), fds);
    fih_close(out_pipe[1]);
    fih_close(in_pipe[0]);
    if (pid < 0) {
        fih_close(out_pipe[0]);
        fih_close(in_pipe[1]);
        return err(argv[0], pid);
    }
    child_pid = pid;
    child_out = out_pipe[0];
    child_in = in_pipe[1];
    child_started = fih_uptime();
}

void run_builtin(int argc, char** argv) {
    const char* c = argv[0];
    int e;
    if (!strcmp(c, "help")) cmd_help();
    else if (!strcmp(c, "ls")) {
        bool lng = argc > 1 && !strcmp(argv[1], "-l");
        int first = lng ? 2 : 1;
        if (argc <= first) list_dir(".", lng);
        for (int i = first; i < argc; i++) {
            if (argc - first > 1) outf(C_DIM, "%s:\n", argv[i]);
            list_dir(argv[i], lng);
        }
    } else if (!strcmp(c, "cd")) {
        const char* to = argc > 1 ? argv[1] : (fs::is_dir("/home") ? "/home" : "/");
        if ((e = fih_chdir(to))) err(to, e);
    } else if (!strcmp(c, "pwd")) {
        char cwd[FIH_PATH_MAX];
        fih_getcwd(cwd, sizeof cwd);
        outf(C_TEXT, "%s\n", cwd);
    } else if (!strcmp(c, "mkdir")) {
        if (argc < 2) out("usage: mkdir path...\n", -1, C_ERROR);
        for (int i = 1; i < argc; i++) if ((e = fih_mkdir(argv[i]))) err(argv[i], e);
    } else if (!strcmp(c, "touch")) {
        for (int i = 1; i < argc; i++) {
            int fd = fih_open(argv[i], FIH_O_WRITE | FIH_O_CREATE);
            if (fd < 0) err(argv[i], fd);
            else fih_close(fd);
        }
    } else if (!strcmp(c, "cat")) {
        cmd_cat(argc, argv);
    } else if (!strcmp(c, "rm")) {
        bool rec = argc > 1 && !strcmp(argv[1], "-r");
        for (int i = rec ? 2 : 1; i < argc; i++) {
            e = rec ? fs::remove_tree(argv[i]) : fih_unlink(argv[i]);
            if (e == -FIH_ENOTEMPTY) outf(C_ERROR, "%s: folder is not empty (use rm -r)\n", argv[i]);
            else if (e) err(argv[i], e);
        }
    } else if (!strcmp(c, "mv")) {
        if (argc != 3) { out("usage: mv from to\n", -1, C_ERROR); return; }
        char to[FIH_PATH_MAX];
        if (fs::is_dir(argv[2])) fs::join(argv[2], fs::basename(argv[1]), to, sizeof to);
        else strlcpy(to, argv[2], sizeof to);
        e = fih_rename(argv[1], to, 0);
        if (e == -FIH_EXDEV) {
            e = fs::copy(argv[1], to);
            if (!e) e = fs::remove_tree(argv[1]);
        }
        if (e) err("mv", e);
    } else if (!strcmp(c, "cp")) {
        bool rec = argc > 1 && !strcmp(argv[1], "-r");
        int a = rec ? 2 : 1;
        if (argc != a + 2) { out("usage: cp [-r] from to\n", -1, C_ERROR); return; }
        if (fs::is_dir(argv[a]) && !rec) { outf(C_ERROR, "%s is a folder (use cp -r)\n", argv[a]); return; }
        char to[FIH_PATH_MAX];
        if (fs::is_dir(argv[a + 1])) fs::join(argv[a + 1], fs::basename(argv[a]), to, sizeof to);
        else strlcpy(to, argv[a + 1], sizeof to);
        if ((e = fs::copy(argv[a], to))) err("cp", e);
    } else if (!strcmp(c, "echo")) {
        for (int i = 1; i < argc; i++) { out(argv[i]); if (i < argc - 1) out(" "); }
        out("\n");
    } else if (!strcmp(c, "clear")) {
        nrows = 0;
        first_row = 0;
        out_col = 0;
    } else if (!strcmp(c, "sysinfo")) cmd_sysinfo();
    else if (!strcmp(c, "ps")) cmd_ps();
    else if (!strcmp(c, "df")) cmd_df();
    else if (!strcmp(c, "kill")) {
        for (int i = 1; i < argc; i++) if ((e = fih_kill(atoi(argv[i])))) outf(C_ERROR, "kill %s: no such process\n", argv[i]);
    } else if (!strcmp(c, "date")) {
        fih_time_t t;
        fih_time(&t);
        outf(C_TEXT, "%04u-%02u-%02u %02u:%02u:%02u\n", t.year, t.month, t.day, t.hour, t.minute, t.second);
    } else if (!strcmp(c, "uptime")) {
        uint32_t s = fih_uptime() / 1000;
        outf(C_TEXT, "up %uh %02um %02us\n", s / 3600, (s / 60) % 60, s % 60);
    } else if (!strcmp(c, "edit") || !strcmp(c, "open")) {
        const char* target = argc > 1 ? argv[1] : nullptr;
        char abs[FIH_PATH_MAX] = "";
        if (target) fs::absolute(target, abs, sizeof abs);
        bool dir = target && fs::is_dir(abs);
        int pid = ui::launch(dir && !strcmp(c, "open") ? "/system/bin/files" : "/system/bin/edit", target ? abs : nullptr);
        if (pid < 0) err(c, pid);
    } else if (!strcmp(c, "reboot")) {
        fih_power(FIH_POWER_REBOOT);
    } else if (!strcmp(c, "shutdown")) {
        e = fih_power(FIH_POWER_SHUTDOWN);
        err("shutdown", e);
    } else if (!strcmp(c, "exit")) {
        win.close();
        fih_exit(0);
    } else {
        start_external(argc, argv);
    }
}

void execute(char* cmdline) {
    // Optional output redirection: `cmd > file` or `cmd >> file`.
    char* redirect = strchr(cmdline, '>');
    bool append = false;
    char target[FIH_PATH_MAX] = "";
    if (redirect) {
        append = redirect[1] == '>';
        *redirect = 0;
        char* t = redirect + (append ? 2 : 1);
        while (*t == ' ') t++;
        strlcpy(target, t, sizeof target);
        for (int i = (int)strlen(target) - 1; i >= 0 && target[i] == ' '; i--) target[i] = 0;
        if (!target[0]) { out("missing file after '>'\n", -1, C_ERROR); return; }
    }
    char* argv[33];
    int argc = split(cmdline, argv, 33);
    if (!argc) return;
    if (target[0]) {
        capture = static_cast<char*>(malloc(256));
        capture_cap = capture ? 256 : 0;
        capture_len = 0;
    }
    run_builtin(argc, argv);
    if (capture) {
        char* data = capture;
        int len = capture_len;
        capture = nullptr;
        int fd = fih_open(target, FIH_O_WRITE | FIH_O_CREATE | (append ? FIH_O_APPEND : FIH_O_TRUNC));
        if (fd < 0) err(target, fd);
        else {
            int n = fih_write(fd, data, (uint32_t)len);
            if (n < 0) err(target, n);
            fih_close(fd);
        }
        free(data);
    }
}

void finish_child() {
    char buf[512];
    int n;
    while ((n = fih_read(child_out, buf, sizeof buf)) > 0) out(buf, n);
    int code = 0;
    fih_wait(child_pid, &code, 0);
    fih_close(child_out);
    if (child_in >= 0) fih_close(child_in);
    child_pid = child_out = child_in = -1;
    if (code) outf(C_ERROR, "%s[exit code %d]\n", out_col ? "\n" : "", code);
    line_len = line_cursor = 0;
    prompt();
}

void poll_child() {
    if (child_pid < 0) return;
    char buf[512];
    int n;
    while ((n = fih_read(child_out, buf, sizeof buf)) > 0) out(buf, n);
    int code;
    if (fih_wait(child_pid, &code, FIH_WNOHANG) == child_pid) {
        // Already reaped: report through the same path.
        while ((n = fih_read(child_out, buf, sizeof buf)) > 0) out(buf, n);
        fih_close(child_out);
        if (child_in >= 0) fih_close(child_in);
        child_pid = child_out = child_in = -1;
        if (code) outf(C_ERROR, "%s[exit code %d]\n", out_col ? "\n" : "", code);
        line_len = line_cursor = 0;
        prompt();
    } else if (n == 0) {
        finish_child(); // output closed: the program is ending
    }
}

void complete() {
    // Complete the last word against entries of its folder.
    int start = line_len;
    while (start > 0 && line[start - 1] != ' ') start--;
    char word[256];
    memcpy(word, line + start, (size_t)(line_len - start));
    word[line_len - start] = 0;
    char dir[FIH_PATH_MAX] = ".";
    const char* prefix = word;
    if (char* slash = strrchr(word, '/')) {
        *slash = 0;
        strlcpy(dir, word[0] ? word : "/", sizeof dir);
        prefix = slash + 1;
        *slash = '/';
    }
    int fd = fih_open(dir, FIH_O_READ);
    if (fd < 0) return;
    fih_dirent_t d, match;
    int matches = 0;
    size_t plen = strlen(prefix);
    for (uint32_t i = 0; fih_readdir(fd, i, &d) == 1; i++)
        if (!strncmp(d.name, prefix, plen)) { match = d; matches++; }
    fih_close(fd);
    if (matches != 1) return;
    const char* rest = match.name + plen;
    for (const char* p = rest; *p && line_len < 250; p++) line[line_len++] = *p;
    if (match.type == FIH_FT_DIR && line_len < 254) line[line_len++] = '/';
    line_cursor = line_len;
}

void on_key(const Event& e) {
    if (e.shift() && (e.key == FIH_KEY_PGUP || e.key == FIH_KEY_PGDN)) {
        view_offset += e.key == FIH_KEY_PGUP ? visible_rows() / 2 : -visible_rows() / 2;
        int max = nrows - visible_rows();
        if (view_offset > max) view_offset = max > 0 ? max : 0;
        if (view_offset < 0) view_offset = 0;
        return;
    }
    view_offset = 0;
    if (e.ctrl() && e.shift() && (e.ch | 32) == 'v') {
        int n;
        char* clip = ui::clipboard_get(&n);
        for (int i = 0; clip && i < n && line_len < 250; i++)
            if (clip[i] >= 32 && clip[i] < 127) {
                memmove(line + line_cursor + 1, line + line_cursor, (size_t)(line_len - line_cursor));
                line[line_cursor++] = clip[i];
                line_len++;
            }
        free(clip);
        return;
    }
    if (child_pid >= 0) {
        if (e.ctrl() && (e.ch | 32) == 'c') {
            fih_kill(child_pid);
            out("^C\n", -1, C_DIM);
            return;
        }
        if (e.ctrl() && (e.ch | 32) == 'd' && child_in >= 0) { fih_close(child_in); child_in = -1; return; }
        // Line-buffered input for the program.
        if (e.key == FIH_KEY_ENTER && child_in >= 0) {
            line[line_len++] = '\n';
            fih_write(child_in, line, (uint32_t)line_len);
            out(line, line_len);
            line_len = line_cursor = 0;
        } else if (e.key == FIH_KEY_BACKSPACE && line_len) {
            line_len--;
            line_cursor = line_len;
        } else if (e.ch >= 32 && e.ch < 127 && !e.ctrl() && line_len < 250) {
            line[line_len++] = (char)e.ch;
            line_cursor = line_len;
        }
        return;
    }
    if (e.ctrl()) {
        switch (e.ch | 32) {
        case 'c': out(line, line_len); out("^C", -1, C_DIM); line_len = line_cursor = 0; prompt(); return;
        case 'l': nrows = first_row = out_col = 0; prompt(); return;
        case 'a': line_cursor = 0; return;
        case 'e': line_cursor = line_len; return;
        case 'u': line_len = line_cursor = 0; return;
        }
        return;
    }
    switch (e.key) {
    case FIH_KEY_ENTER: {
        line[line_len] = 0;
        out(line, line_len);
        out("\n");
        if (line_len && (nhist == 0 || strcmp(history[(nhist - 1) % 32], line))) {
            strlcpy(history[nhist % 32], line, sizeof history[0]);
            nhist++;
        }
        hist_pos = -1;
        char cmd[256];
        strlcpy(cmd, line, sizeof cmd);
        line_len = line_cursor = 0;
        execute(cmd);
        if (child_pid < 0) prompt();
        return;
    }
    case FIH_KEY_BACKSPACE:
        if (line_cursor > 0) {
            memmove(line + line_cursor - 1, line + line_cursor, (size_t)(line_len - line_cursor));
            line_cursor--;
            line_len--;
        }
        return;
    case FIH_KEY_DELETE:
        if (line_cursor < line_len) {
            memmove(line + line_cursor, line + line_cursor + 1, (size_t)(line_len - line_cursor - 1));
            line_len--;
        }
        return;
    case FIH_KEY_LEFT: if (line_cursor > 0) line_cursor--; return;
    case FIH_KEY_RIGHT: if (line_cursor < line_len) line_cursor++; return;
    case FIH_KEY_HOME: line_cursor = 0; return;
    case FIH_KEY_END: line_cursor = line_len; return;
    case FIH_KEY_TAB: complete(); return;
    case FIH_KEY_UP: case FIH_KEY_DOWN: {
        int avail = nhist < 32 ? nhist : 32;
        if (!avail) return;
        if (e.key == FIH_KEY_UP) hist_pos = hist_pos < 0 ? 0 : (hist_pos + 1 < avail ? hist_pos + 1 : hist_pos);
        else hist_pos = hist_pos > 0 ? hist_pos - 1 : -1;
        if (hist_pos < 0) line_len = 0;
        else line_len = (int)strlcpy(line, history[(nhist - 1 - hist_pos) % 32], sizeof line);
        line_cursor = line_len;
        return;
    }
    }
    if (e.ch >= 32 && e.ch < 127 && !e.alt() && line_len < 250) {
        memmove(line + line_cursor + 1, line + line_cursor, (size_t)(line_len - line_cursor));
        line[line_cursor++] = (char)e.ch;
        line_len++;
    }
}
}

int main(int argc, char** argv) {
    rows = static_cast<Row*>(malloc(sizeof(Row) * SCROLLBACK));
    if (!rows || !win.open("Terminal", 720, 440)) return 1;
    if (argc > 1) fih_chdir(argv[1]);
    out("FishOS Terminal", -1, C_PROMPT);
    out(" - type 'help' for commands\n", -1, C_DIM);
    prompt();
    draw();
    for (;;) {
        ui::Window* w;
        Event e;
        bool got = ui::next_event(&w, &e, child_pid >= 0 ? 15 : -1);
        if (got) {
            if (e.type == Event::Key) on_key(e);
            else if (e.type == Event::Wheel) {
                view_offset -= e.wheel * 3;
                int max = nrows - visible_rows();
                if (view_offset > max) view_offset = max > 0 ? max : 0;
                if (view_offset < 0) view_offset = 0;
            } else if (e.type == Event::Close) {
                if (child_pid >= 0) fih_kill(child_pid);
                win.close();
                return 0;
            }
        }
        poll_child();
        draw();
    }
}
