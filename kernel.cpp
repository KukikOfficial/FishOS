#include <stdint.h>
#include <stddef.h>
#include "font.hpp"

// --- СТРУКТУРЫ MULTIBOOT 1 ---
struct multiboot_module {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t string;
    uint32_t reserved;
} __attribute__((packed));

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint8_t  color_info[6];
} __attribute__((packed));

// --- ПОРТЫ ВВОДА-ВЫВОДА ---
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// --- СТРОКОВЫЕ УТИЛИТЫ ---
int kstrlen(const char* s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

void kstrcpy(char* dest, const char* src) {
    while ((*dest++ = *src++));
}

bool kstrcmp(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return false;
        a++; b++;
    }
    return (*a == *b);
}

void int_to_str(int n, char* buf) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    int i = 0;
    bool neg = (n < 0);
    if (neg) n = -n;
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    if (neg) buf[i++] = '-';
    buf[i] = '\0';
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i - 1 - j]; buf[i - 1 - j] = t;
    }
}

// --- CMOS RTC (АППАРАТНЫЕ ЧАСЫ) ---
static uint8_t rtc_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static uint8_t bcd_to_bin(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

void get_rtc_time(int& h, int& m, int& s) {
    outb(0x70, 0x0A);
    while (inb(0x71) & 0x80);
    uint8_t regb = rtc_read(0x0B);
    s = rtc_read(0x00);
    m = rtc_read(0x02);
    h = rtc_read(0x04);
    if (!(regb & 0x04)) {
        s = bcd_to_bin(s);
        m = bcd_to_bin(m);
        h = bcd_to_bin(h & 0x7F) | (h & 0x80);
    }
}

// --- TAR VFS (ВИРТУАЛЬНАЯ ФАЙЛОВАЯ СИСТЕМА) ---
struct tar_header {
    char filename[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag[1];
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};

constexpr int MAX_FILES = 16;
constexpr int MAX_FILE_SIZE = 2048;

struct VFSFile {
    char name[48];
    uint32_t size;
    char content[MAX_FILE_SIZE];
    bool used;
};

static VFSFile vfs[MAX_FILES];
static int vfs_file_count = 0;

static uint32_t oct2bin(const char* str, int size) {
    uint32_t n = 0;
    for (int i = 0; i < size; i++) {
        if (str[i] >= '0' && str[i] <= '7') {
            n = (n << 3) + (str[i] - '0');
        } else if (str[i] == ' ' || str[i] == '\0') {
            if (n > 0) break;
        }
    }
    return n;
}

void vfs_add_file(const char* name, const char* data, uint32_t size) {
    if (vfs_file_count >= MAX_FILES) return;
    VFSFile& f = vfs[vfs_file_count++];
    f.used = true;
    f.size = (size < MAX_FILE_SIZE - 1) ? size : MAX_FILE_SIZE - 1;

    if (name[0] == '.' && name[1] == '/') name += 2;
    kstrcpy(f.name, name);

    for (uint32_t i = 0; i < f.size; i++) f.content[i] = data[i];
    f.content[f.size] = '\0';
}

void vfs_save_file(const char* name, const char* data, uint32_t size) {
    for (int i = 0; i < vfs_file_count; i++) {
        if (kstrcmp(vfs[i].name, name)) {
            vfs[i].size = (size < MAX_FILE_SIZE - 1) ? size : MAX_FILE_SIZE - 1;
            for (uint32_t j = 0; j < vfs[i].size; j++) vfs[i].content[j] = data[j];
            vfs[i].content[vfs[i].size] = '\0';
            return;
        }
    }
    vfs_add_file(name, data, size);
}

void vfs_init(uint32_t mod_start, uint32_t mod_end) {
    vfs_file_count = 0;
    if (mod_start != 0 && mod_end > mod_start) {
        uint32_t ptr = mod_start;
        while (ptr < mod_end) {
            auto* hdr = reinterpret_cast<tar_header*>(ptr);
            if (hdr->filename[0] == '\0') break;

            uint32_t sz = oct2bin(hdr->size, 11);
            if (hdr->typeflag[0] == '0' || hdr->typeflag[0] == '\0') {
                const char* data = reinterpret_cast<const char*>(ptr + 512);
                vfs_add_file(hdr->filename, data, sz);
            }
            ptr += 512 + ((sz + 511) / 512) * 512;
        }
    }

    if (vfs_file_count == 0) {
        vfs_add_file("readme.txt", "Welcome to LiteOS RAM VFS!\nType something here.", 46);
        vfs_add_file("todo.txt", "1. Open Paint\n2. Draw some art\n3. Enjoy your OS!", 47);
    }
}

// --- ГРАФИКА И ДВОЙНАЯ БУФЕРИЗАЦИЯ ---
constexpr int SCREEN_W = 1024;
constexpr int SCREEN_H = 768;
static uint32_t backbuffer[SCREEN_W * SCREEN_H];

static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

void put_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H) {
        backbuffer[y * SCREEN_W + x] = color;
    }
}

void fill_rect(int x, int y, int w, int h, uint32_t color) {
    int x1 = (x < 0) ? 0 : x;
    int y1 = (y < 0) ? 0 : y;
    int x2 = (x + w > SCREEN_W) ? SCREEN_W : x + w;
    int y2 = (y + h > SCREEN_H) ? SCREEN_H : y + h;
    for (int cy = y1; cy < y2; cy++) {
        uint32_t* row = &backbuffer[cy * SCREEN_W];
        for (int cx = x1; cx < x2; cx++) row[cx] = color;
    }
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
    fill_rect(x, y, w, 1, color);
    fill_rect(x, y + h - 1, w, 1, color);
    fill_rect(x, y, 1, h, color);
    fill_rect(x + w - 1, y, 1, h, color);
}

void draw_gradient_v(int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    int r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    int r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    for (int cy = 0; cy < h; cy++) {
        int r = r1 + ((r2 - r1) * cy) / h;
        int g = g1 + ((g2 - g1) * cy) / h;
        int b = b1 + ((b2 - b1) * cy) / h;
        fill_rect(x, y + cy, w, 1, rgb(r, g, b));
    }
}

void draw_shadow(int x, int y, int w, int h) {
    for (int sy = 0; sy < h; sy++) {
        for (int sx = 0; sx < w; sx++) {
            int px = x + sx, py = y + sy;
            if (px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H) {
                uint32_t orig = backbuffer[py * SCREEN_W + px];
                backbuffer[py * SCREEN_W + px] = rgb(((orig >> 16) & 0xFF) / 2, ((orig >> 8) & 0xFF) / 2, (orig & 0xFF) / 2);
            }
        }
    }
}

void draw_char(int x, int y, char c, uint32_t fg) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t* glyph = font8x8[c - 32];
    for (int cy = 0; cy < 8; cy++) {
        uint8_t row = glyph[cy];
        for (int cx = 0; cx < 8; cx++) {
            if ((row >> (7 - cx)) & 1) put_pixel(x + cx, y + cy, fg);
        }
    }
}

void draw_string(int x, int y, const char* str, uint32_t fg) {
    int cur_x = x, cur_y = y;
    while (*str) {
        if (*str == '\n') {
            cur_x = x;
            cur_y += 12;
        } else {
            draw_char(cur_x, cur_y, *str, fg);
            cur_x += 8;
        }
        str++;
    }
}

// --- КУРСОР МЫШИ ---
static const char* cursor_sprite[16] = {
    "X               ",
    "XX              ",
    "X.X             ",
    "X..X            ",
    "X...X           ",
    "X....X          ",
    "X.....X         ",
    "X......X        ",
    "X.......X       ",
    "X.....XXXX      ",
    "X..X..X         ",
    "X.X X..X        ",
    "XX   X..X       ",
    "X     X..X      ",
    "       XX       ",
    "                "
};

void draw_cursor(int mx, int my) {
    for (int cy = 0; cy < 16; cy++) {
        for (int cx = 0; cx < 16; cx++) {
            char p = cursor_sprite[cy][cx];
            if (p == 'X') put_pixel(mx + cx, my + cy, 0x000000);
            else if (p == '.') put_pixel(mx + cx, my + cy, 0xFFFFFF);
        }
    }
}

// --- СОСТОЯНИЕ ПРИЛОЖЕНИЙ ---
// 1. Блокнот
char notepad_buffer[MAX_FILE_SIZE] = "Welcome to LiteOS Notepad!\nClick anywhere in this window and start typing.";
int notepad_cursor = 72;
char notepad_filename[48] = "readme.txt";

// 2. Paint
constexpr int PAINT_W = 310;
constexpr int PAINT_H = 180;
static uint32_t paint_canvas[PAINT_W * PAINT_H];
uint32_t palette[6] = { 0x000000, 0xEF4444, 0x22C55E, 0x3B82F6, 0xEAB308, 0xFFFFFF };
int active_color_idx = 0;

void clear_canvas() {
    for (int i = 0; i < PAINT_W * PAINT_H; i++) {
        paint_canvas[i] = 0xFFFFFF;
    }
}

// 3. Оконный менеджер
struct Window {
    int id; // 0 = Explorer, 1 = Notepad, 2 = Paint
    int x, y, w, h;
    const char* title;
    bool closed;
};

Window windows[3] = {
    { 0,  40,  50, 320, 340, "File Explorer [VFS]", false },
    { 1, 380,  50, 500, 340, "Text Editor (Notepad)", false },
    { 2, 200, 380, 334, 250, "LitePaint Studio", false }
};

int z_order[3] = { 0, 1, 2 };
int mouse_x = SCREEN_W / 2;
int mouse_y = SCREEN_H / 2;
bool mouse_btn_left = false;
int dragging_window = -1;
int drag_offset_x = 0;
int drag_offset_y = 0;
bool start_menu_open = false;
bool shift_pressed = false;
int blink_timer = 0;

// Отрисовка окон и содержимого
void draw_window(const Window& win, bool active) {
    if (win.closed) return;

    draw_shadow(win.x + 6, win.y + 6, win.w, win.h);
    fill_rect(win.x, win.y, win.w, win.h, 0xF8FAFC);
    draw_rect(win.x, win.y, win.w, win.h, 0x475569);

    // Заголовок окна
    draw_gradient_v(win.x + 1, win.y + 1, win.w - 2, 24, active ? 0x2563EB : 0x64748B, active ? 0x1E40AF : 0x334155);
    draw_string(win.x + 8, win.y + 7, win.title, 0xFFFFFF);

    // Кнопка закрытия [X]
    int btn_x = win.x + win.w - 20;
    int btn_y = win.y + 4;
    fill_rect(btn_x, btn_y, 16, 16, 0xEF4444);
    draw_string(btn_x + 4, btn_y + 4, "X", 0xFFFFFF);

    int cx = win.x + 12;
    int cy = win.y + 34;

    // --- 0: ФАЙЛОВЫЙ МЕНЕДЖЕР ---
    if (win.id == 0) {
        draw_string(cx, cy, "Files in Memory (TAR VFS):", 0x0F172A);
        cy += 18;

        for (int i = 0; i < vfs_file_count; i++) {
            bool is_cur = kstrcmp(notepad_filename, vfs[i].name);
            fill_rect(cx, cy, win.w - 24, 24, is_cur ? 0xDBEAFE : 0xFFFFFF);
            draw_rect(cx, cy, win.w - 24, 24, is_cur ? 0x3B82F6 : 0xCBD5E1);

            draw_string(cx + 8, cy + 7, vfs[i].name, 0x0F172A);

            char sz_buf[16];
            int_to_str(vfs[i].size, sz_buf);
            draw_string(cx + 190, cy + 7, sz_buf, 0x64748B);
            draw_string(cx + 240, cy + 7, "B", 0x64748B);

            cy += 28;
        }

        // Кнопка создания файла
        int n_btn_y = win.y + win.h - 32;
        fill_rect(cx, n_btn_y, 110, 24, 0x10B981);
        draw_rect(cx, n_btn_y, 110, 24, 0x059669);
        draw_string(cx + 12, n_btn_y + 7, "+ New File", 0xFFFFFF);
    }
    // --- 1: БЛОКНОТ ---
    else if (win.id == 1) {
        // Верхняя панель: имя файла и кнопка сохранения
        draw_string(cx, cy + 4, "File: ", 0x64748B);
        draw_string(cx + 42, cy + 4, notepad_filename, 0x0F172A);

        int save_btn_x = win.x + win.w - 110;
        fill_rect(save_btn_x, cy, 98, 22, 0x2563EB);
        draw_rect(save_btn_x, cy, 98, 22, 0x1D4ED8);
        draw_string(save_btn_x + 10, cy + 6, "[Save File]", 0xFFFFFF);

        cy += 28;

        // Область редактора текста
        int edit_w = win.w - 24;
        int edit_h = win.h - 72;
        fill_rect(cx, cy, edit_w, edit_h, 0xFFFFFF);
        draw_rect(cx, cy, edit_w, edit_h, 0x94A3B8);

        // Отрисовка текста
        int text_x = cx + 6;
        int text_y = cy + 6;
        int cur_x = text_x;
        int cur_y = text_y;

        for (int i = 0; notepad_buffer[i] != '\0'; i++) {
            if (i == notepad_cursor && (blink_timer % 40 < 20)) {
                fill_rect(cur_x, cur_y, 2, 10, 0x2563EB);
            }
            if (notepad_buffer[i] == '\n') {
                cur_x = text_x;
                cur_y += 12;
            } else {
                draw_char(cur_x, cur_y, notepad_buffer[i], 0x0F172A);
                cur_x += 8;
                if (cur_x > cx + edit_w - 12) {
                    cur_x = text_x;
                    cur_y += 12;
                }
            }
        }
        // Каретка в самом конце текста
        if (notepad_cursor == kstrlen(notepad_buffer) && (blink_timer % 40 < 20)) {
            fill_rect(cur_x, cur_y, 2, 10, 0x2563EB);
        }
    }
    // --- 2: PAINT ---
    else if (win.id == 2) {
        // Палитра
        int pal_x = cx;
        for (int i = 0; i < 6; i++) {
            fill_rect(pal_x, cy, 20, 20, palette[i]);
            draw_rect(pal_x, cy, 20, 20, (active_color_idx == i) ? 0x000000 : 0x94A3B8);
            if (active_color_idx == i) {
                draw_rect(pal_x + 2, cy + 2, 16, 16, 0xFFFFFF);
            }
            pal_x += 26;
        }

        // Кнопка очистки холста
        int clr_x = win.x + win.w - 80;
        fill_rect(clr_x, cy, 68, 20, 0xE2E8F0);
        draw_rect(clr_x, cy, 68, 20, 0x94A3B8);
        draw_string(clr_x + 8, cy + 5, "Clear", 0x0F172A);

        cy += 26;

        // Холст
        draw_rect(cx - 1, cy - 1, PAINT_W + 2, PAINT_H + 2, 0x64748B);
        for (int py = 0; py < PAINT_H; py++) {
            uint32_t* row = &backbuffer[(cy + py) * SCREEN_W + cx];
            const uint32_t* src_row = &paint_canvas[py * PAINT_W];
            for (int px = 0; px < PAINT_W; px++) {
                row[px] = src_row[px];
            }
        }
    }
}

// --- PS/2 МЫШЬ ---
void mouse_wait_write() {
    uint32_t t = 100000;
    while (t-- && (inb(0x64) & 2));
}

void mouse_wait_read() {
    uint32_t t = 100000;
    while (t-- && !(inb(0x64) & 1));
}

void mouse_write(uint8_t val) {
    mouse_wait_write();
    outb(0x64, 0xD4);
    mouse_wait_write();
    outb(0x60, val);
}

uint8_t mouse_read() {
    mouse_wait_read();
    return inb(0x60);
}

void mouse_init() {
    mouse_wait_write();
    outb(0x64, 0xA8);

    mouse_wait_write();
    outb(0x64, 0x20);
    uint8_t status = mouse_read();
    status &= ~(1 << 5);
    status &= ~(1 << 1);
    status &= ~(1 << 0);

    mouse_wait_write();
    outb(0x64, 0x60);
    mouse_wait_write();
    outb(0x60, status);

    mouse_write(0xF6); mouse_read();
    mouse_write(0xF4); mouse_read();
}

// Преобразование скан-кода PS/2 в ASCII
char scancode_to_char(uint8_t sc, bool shift) {
    switch (sc) {
        case 0x02: return shift ? '!' : '1';
        case 0x03: return shift ? '@' : '2';
        case 0x04: return shift ? '#' : '3';
        case 0x05: return shift ? '$' : '4';
        case 0x06: return shift ? '%' : '5';
        case 0x07: return shift ? '^' : '6';
        case 0x08: return shift ? '&' : '7';
        case 0x09: return shift ? '*' : '8';
        case 0x0A: return shift ? '(' : '9';
        case 0x0B: return shift ? ')' : '0';
        case 0x0C: return shift ? '_' : '-';
        case 0x0D: return shift ? '+' : '=';
        case 0x10: return shift ? 'Q' : 'q';
        case 0x11: return shift ? 'W' : 'w';
        case 0x12: return shift ? 'E' : 'e';
        case 0x13: return shift ? 'R' : 'r';
        case 0x14: return shift ? 'T' : 't';
        case 0x15: return shift ? 'Y' : 'y';
        case 0x16: return shift ? 'U' : 'u';
        case 0x17: return shift ? 'I' : 'i';
        case 0x18: return shift ? 'O' : 'o';
        case 0x19: return shift ? 'P' : 'p';
        case 0x1E: return shift ? 'A' : 'a';
        case 0x1F: return shift ? 'S' : 's';
        case 0x20: return shift ? 'D' : 'd';
        case 0x21: return shift ? 'F' : 'f';
        case 0x22: return shift ? 'G' : 'g';
        case 0x23: return shift ? 'H' : 'h';
        case 0x24: return shift ? 'J' : 'j';
        case 0x25: return shift ? 'K' : 'k';
        case 0x26: return shift ? 'L' : 'l';
        case 0x2C: return shift ? 'Z' : 'z';
        case 0x2D: return shift ? 'X' : 'x';
        case 0x2E: return shift ? 'C' : 'c';
        case 0x2F: return shift ? 'V' : 'v';
        case 0x30: return shift ? 'B' : 'b';
        case 0x31: return shift ? 'N' : 'n';
        case 0x32: return shift ? 'M' : 'm';
        case 0x39: return ' ';
        case 0x1C: return '\n';
        case 0x33: return shift ? '<' : ',';
        case 0x34: return shift ? '>' : '.';
        case 0x35: return shift ? '?' : '/';
        default: return 0;
    }
}

void process_inputs() {
    static int cycle = 0;
    static uint8_t packet[3];

    while (true) {
        uint8_t st = inb(0x64);
        if (!(st & 1)) break;

        uint8_t data = inb(0x60);

        if (st & 0x20) {
            // Данные мыши
            switch (cycle) {
                case 0:
                    if (data & 0x08) { packet[0] = data; cycle = 1; }
                    break;
                case 1:
                    packet[1] = data; cycle = 2;
                    break;
                case 2:
                    packet[2] = data; cycle = 0;
                    mouse_btn_left = (packet[0] & 1);

                    int dx = packet[1];
                    if (packet[0] & 0x10) dx -= 256;
                    int dy = packet[2];
                    if (packet[0] & 0x20) dy -= 256;

                    mouse_x += dx;
                    mouse_y -= dy;

                    if (mouse_x < 0) mouse_x = 0;
                    if (mouse_x >= SCREEN_W) mouse_x = SCREEN_W - 1;
                    if (mouse_y < 0) mouse_y = 0;
                    if (mouse_y >= SCREEN_H) mouse_y = SCREEN_H - 1;
                    break;
            }
        } else {
            // Данные клавиатуры
            if (data == 0x2A || data == 0x36) shift_pressed = true;
            else if (data == 0xAA || data == 0xB6) shift_pressed = false;
            else if (!(data & 0x80)) {
                // Если активно окно Notepad (id 1) и оно на переднем плане
                if (z_order[2] == 1 && !windows[1].closed) {
                    if (data == 0x0E) {
                        // Backspace
                        if (notepad_cursor > 0) {
                            int len = kstrlen(notepad_buffer);
                            for (int i = notepad_cursor - 1; i < len; i++) {
                                notepad_buffer[i] = notepad_buffer[i + 1];
                            }
                            notepad_cursor--;
                        }
                    } else {
                        char c = scancode_to_char(data, shift_pressed);
                        if (c != 0) {
                            int len = kstrlen(notepad_buffer);
                            if (len < MAX_FILE_SIZE - 2) {
                                for (int i = len; i >= notepad_cursor; i--) {
                                    notepad_buffer[i + 1] = notepad_buffer[i];
                                }
                                notepad_buffer[notepad_cursor++] = c;
                            }
                        }
                    }
                }

                // Управление курсором стрелками
                if (data == 0x48) mouse_y -= 10;
                if (data == 0x50) mouse_y += 10;
                if (data == 0x4B) mouse_x -= 10;
                if (data == 0x4D) mouse_x += 10;
                if (data == 0x1C) mouse_btn_left = true;
            } else if (data == 0x9C) {
                mouse_btn_left = false;
            }
        }
    }
}

// --- СИСТЕМНАЯ ТОЧКА ВХОДА ---
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

    // Инициализация файловой системы TAR VFS из модуля GRUB
    uint32_t mod_start = 0, mod_end = 0;
    if ((mbi->flags & (1 << 3)) && mbi->mods_count > 0) {
        auto* mod = reinterpret_cast<multiboot_module*>(mbi->mods_addr);
        mod_start = mod->mod_start;
        mod_end = mod->mod_end;
    }
    vfs_init(mod_start, mod_end);

    // Загрузка первого файла в редактор
    if (vfs_file_count > 0) {
        kstrcpy(notepad_filename, vfs[0].name);
        kstrcpy(notepad_buffer, vfs[0].content);
        notepad_cursor = kstrlen(notepad_buffer);
    }

    clear_canvas();
    mouse_init();

    bool prev_lclick = false;

    // Главный цикл ОС
    while (true) {
        blink_timer++;
        process_inputs();

        bool lclick_pressed = (!prev_lclick && mouse_btn_left);
        bool lclick_released = (prev_lclick && !mouse_btn_left);
        prev_lclick = mouse_btn_left;

        // --- ОБРАБОТКА PAINT ПРИ ЗАЖАТОЙ ЛКМ ---
        if (mouse_btn_left && z_order[2] == 2 && !windows[2].closed) {
            int cx = windows[2].x + 12;
            int cy = windows[2].y + 60;
            if (mouse_x >= cx && mouse_x < cx + PAINT_W && mouse_y >= cy && mouse_y < cy + PAINT_H) {
                int px = mouse_x - cx;
                int py = mouse_y - cy;
                // Кисть размером 2x2 пикселя
                for (int dy = 0; dy <= 1; dy++) {
                    for (int dx = 0; dx <= 1; dx++) {
                        if (px + dx < PAINT_W && py + dy < PAINT_H) {
                            paint_canvas[(py + dy) * PAINT_W + (px + dx)] = palette[active_color_idx];
                        }
                    }
                }
            }
        }

        // --- ОБРАБОТКА ОДИНОЧНОГО КЛИКА МЫШИ ---
        if (lclick_pressed) {
            // Клик по кнопке «Start»
            if (mouse_x >= 4 && mouse_x <= 84 && mouse_y >= SCREEN_H - 28 && mouse_y <= SCREEN_H - 4) {
                start_menu_open = !start_menu_open;
            }
            // Клик по меню «Start»
            else if (start_menu_open && mouse_x >= 4 && mouse_x <= 164 && mouse_y >= SCREEN_H - 120 && mouse_y < SCREEN_H - 32) {
                if (mouse_y >= SCREEN_H - 60) {
                    outb(0x64, 0xFE); // Перезагрузка ПК
                } else {
                    for (int i = 0; i < 3; i++) windows[i].closed = false;
                    start_menu_open = false;
                }
            } else {
                start_menu_open = false;

                // Проверка окон сверху вниз
                bool handled = false;
                for (int i = 2; i >= 0; i--) {
                    int w_idx = z_order[i];
                    Window& win = windows[w_idx];
                    if (win.closed) continue;

                    if (mouse_x >= win.x && mouse_x < win.x + win.w &&
                        mouse_y >= win.y && mouse_y < win.y + win.h) {

                        // Перемещаем окно наверх списка Z-order
                        for (int k = i; k < 2; k++) z_order[k] = z_order[k + 1];
                        z_order[2] = w_idx;

                        // Кнопка [X]
                        if (mouse_x >= win.x + win.w - 20 && mouse_x <= win.x + win.w - 4 &&
                            mouse_y >= win.y + 4 && mouse_y <= win.y + 20) {
                            win.closed = true;
                        }
                        // Заголовок (перетаскивание)
                        else if (mouse_y <= win.y + 24) {
                            dragging_window = w_idx;
                            drag_offset_x = mouse_x - win.x;
                            drag_offset_y = mouse_y - win.y;
                        }
                        // Кнопки File Explorer
                        else if (win.id == 0) {
                            int file_y = win.y + 52;
                            for (int f = 0; f < vfs_file_count; f++) {
                                if (mouse_x >= win.x + 12 && mouse_x <= win.x + win.w - 12 &&
                                    mouse_y >= file_y && mouse_y <= file_y + 24) {
                                    kstrcpy(notepad_filename, vfs[f].name);
                                    kstrcpy(notepad_buffer, vfs[f].content);
                                    notepad_cursor = kstrlen(notepad_buffer);
                                    windows[1].closed = false; // Открыть блокнот
                                    break;
                                }
                                file_y += 28;
                            }
                            // Кнопка "+ New File"
                            int n_btn_y = win.y + win.h - 32;
                            if (mouse_x >= win.x + 12 && mouse_x <= win.x + 122 &&
                                mouse_y >= n_btn_y && mouse_y <= n_btn_y + 24) {
                                if (vfs_file_count < MAX_FILES) {
                                    char new_name[32] = "newfile";
                                    char idx_str[8];
                                    int_to_str(vfs_file_count + 1, idx_str);
                                    int p = kstrlen(new_name);
                                    for (int s = 0; idx_str[s]; s++) new_name[p++] = idx_str[s];
                                    new_name[p++] = '.'; new_name[p++] = 't'; new_name[p++] = 'x'; new_name[p++] = 't';
                                    new_name[p] = '\0';

                                    vfs_add_file(new_name, "Empty file.\nStart typing...", 25);
                                    kstrcpy(notepad_filename, new_name);
                                    kstrcpy(notepad_buffer, "Empty file.\nStart typing...");
                                    notepad_cursor = kstrlen(notepad_buffer);
                                    windows[1].closed = false;
                                }
                            }
                        }
                        // Кнопки Блокнота
                        else if (win.id == 1) {
                            int save_btn_x = win.x + win.w - 110;
                            if (mouse_x >= save_btn_x && mouse_x <= save_btn_x + 98 &&
                                mouse_y >= win.y + 34 && mouse_y <= win.y + 56) {
                                vfs_save_file(notepad_filename, notepad_buffer, kstrlen(notepad_buffer));
                            }
                        }
                        // Кнопки Paint
                        else if (win.id == 2) {
                            int pal_x = win.x + 12;
                            int pal_y = win.y + 34;
                            for (int p = 0; p < 6; p++) {
                                if (mouse_x >= pal_x && mouse_x <= pal_x + 20 &&
                                    mouse_y >= pal_y && mouse_y <= pal_y + 20) {
                                    active_color_idx = p;
                                }
                                pal_x += 26;
                            }
                            int clr_x = win.x + win.w - 80;
                            if (mouse_x >= clr_x && mouse_x <= clr_x + 68 &&
                                mouse_y >= pal_y && mouse_y <= pal_y + 20) {
                                clear_canvas();
                            }
                        }

                        handled = true;
                        break;
                    }
                }
                if (!handled) dragging_window = -1;
            }
        }

        if (mouse_btn_left && dragging_window != -1) {
            windows[dragging_window].x = mouse_x - drag_offset_x;
            windows[dragging_window].y = mouse_y - drag_offset_y;
        }

        if (lclick_released) {
            dragging_window = -1;
        }

        // --- ОТРИСОВКА КАДРА ---
        // 1. Градиент рабочего стола
        draw_gradient_v(0, 0, SCREEN_W, SCREEN_H - 32, 0x0F172A, 0x1E293B);

        // 2. Окна в порядке Z-Order
        for (int i = 0; i < 3; i++) {
            draw_window(windows[z_order[i]], (i == 2));
        }

        // 3. Панель задач (Taskbar)
        fill_rect(0, SCREEN_H - 32, SCREEN_W, 32, 0x0F172A);
        draw_rect(0, SCREEN_H - 32, SCREEN_W, 1, 0x334155);

        // Кнопка «Start»
        fill_rect(4, SCREEN_H - 28, 80, 24, start_menu_open ? 0x1E40AF : 0x2563EB);
        draw_rect(4, SCREEN_H - 28, 80, 24, 0x38BDF8);
        draw_string(20, SCREEN_H - 20, "Start", 0xFFFFFF);

        // Вкладки окон
        for (int i = 0; i < 3; i++) {
            int tab_x = 94 + i * 140;
            fill_rect(tab_x, SCREEN_H - 28, 130, 24, windows[i].closed ? 0x1E293B : 0x334155);
            draw_rect(tab_x, SCREEN_H - 28, 130, 24, 0x475569);
            char short_title[14];
            for (int s = 0; s < 13; s++) {
                short_title[s] = windows[i].title[s];
                if (!windows[i].title[s]) break;
            }
            short_title[13] = '\0';
            draw_string(tab_x + 8, SCREEN_H - 20, short_title, windows[i].closed ? 0x64748B : 0xFFFFFF);
        }

        // Часы RTC
        int h = 0, m = 0, s = 0;
        get_rtc_time(h, m, s);
        char time_buf[16];
        time_buf[0] = '0' + (h / 10); time_buf[1] = '0' + (h % 10);
        time_buf[2] = ':';
        time_buf[3] = '0' + (m / 10); time_buf[4] = '0' + (m % 10);
        time_buf[5] = ':';
        time_buf[6] = '0' + (s / 10); time_buf[7] = '0' + (s % 10);
        time_buf[8] = '\0';
        draw_string(SCREEN_W - 80, SCREEN_H - 20, time_buf, 0x38BDF8);

        // 4. Меню «Start»
        if (start_menu_open) {
            draw_shadow(4, SCREEN_H - 120, 160, 88);
            fill_rect(4, SCREEN_H - 120, 160, 88, 0x0F172A);
            draw_rect(4, SCREEN_H - 120, 160, 88, 0x38BDF8);

            fill_rect(6, SCREEN_H - 116, 156, 24, 0x1E293B);
            draw_string(14, SCREEN_H - 108, "Restore Windows", 0xFFFFFF);

            fill_rect(6, SCREEN_H - 90, 156, 24, 0x1E293B);
            draw_string(14, SCREEN_H - 82, "LiteOS Terminal", 0xFFFFFF);

            fill_rect(6, SCREEN_H - 64, 156, 26, 0xDC2626);
            draw_string(14, SCREEN_H - 56, "[ Reboot PC ]", 0xFFFFFF);
        }

        // 5. Курсор мыши
        draw_cursor(mouse_x, mouse_y);

        // 6. Копирование кадра в VRAM
        uint32_t pitch_words = pitch_bytes / 4;
        for (int y = 0; y < SCREEN_H; y++) {
            uint32_t* dest = vram + y * pitch_words;
            const uint32_t* src = backbuffer + y * SCREEN_W;
            for (int x = 0; x < SCREEN_W; x++) {
                dest[x] = src[x];
            }
        }
    }
}