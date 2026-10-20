#include <stdint.h>
#include <stddef.h>
#include "font.hpp"

// --- СТРУКТУРА MULTIBOOT 1 ---
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

// --- НИЗКОУРОВНЕВЫЙ ВВОД-ВЫВОД (I/O PORTS) ---
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait() {
    outb(0x80, 0);
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

// --- СИСТЕМА ГРАФИКИ И ДВОЙНАЯ БУФЕРИЗАЦИЯ ---
constexpr int SCREEN_W = 1024;
constexpr int SCREEN_H = 768;

// Back-buffer в секции BSS (не занимает места в бинарнике)
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
        for (int cx = x1; cx < x2; cx++) {
            row[cx] = color;
        }
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
    // Полупрозрачная псевдо-тень
    for (int sy = 0; sy < h; sy++) {
        for (int sx = 0; sx < w; sx++) {
            int px = x + sx;
            int py = y + sy;
            if (px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H) {
                uint32_t orig = backbuffer[py * SCREEN_W + px];
                uint32_t r = ((orig >> 16) & 0xFF) / 2;
                uint32_t g = ((orig >> 8) & 0xFF) / 2;
                uint32_t b = (orig & 0xFF) / 2;
                backbuffer[py * SCREEN_W + px] = rgb(r, g, b);
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
            if ((row >> (7 - cx)) & 1) {
                put_pixel(x + cx, y + cy, fg);
            }
        }
    }
}

void draw_string(int x, int y, const char* str, uint32_t fg) {
    int cur_x = x;
    int cur_y = y;
    while (*str) {
        if (*str == '\n') {
            cur_x = x;
            cur_y += 10;
        } else {
            draw_char(cur_x, cur_y, *str, fg);
            cur_x += 8;
        }
        str++;
    }
}

// Форматирование чисел без libc
void int_to_str(int n, char* buf) {
    if (n == 0) {
        buf[0] = '0'; buf[1] = '\0'; return;
    }
    int i = 0;
    bool neg = (n < 0);
    if (neg) n = -n;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    if (neg) buf[i++] = '-';
    buf[i] = '\0';
    for (int j = 0; j < i / 2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }
}

void hex_to_str(uint32_t val, char* buf) {
    buf[0] = '0'; buf[1] = 'x';
    const char hex_chars[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        buf[2 + (7 - i)] = hex_chars[(val >> (i * 4)) & 0xF];
    }
    buf[10] = '\0';
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

// --- ОКОННЫЙ МЕНЕДЖЕР (GUI) ---
struct Window {
    int id;
    int x, y, w, h;
    const char* title;
    bool closed;
};

Window windows[3] = {
    { 0,  60,  60, 360, 240, "System Information", false },
    { 1, 460,  80, 340, 260, "Interactive Control Panel", false },
    { 2, 160, 340, 560, 220, "Console Log & Inputs", false }
};

int z_order[3] = { 0, 1, 2 }; // Последний индекс — верхнее окно

int mouse_x = SCREEN_W / 2;
int mouse_y = SCREEN_H / 2;
bool mouse_btn_left = false;
bool mouse_btn_right = false;

int dragging_window = -1;
int drag_offset_x = 0;
int drag_offset_y = 0;

int click_counter = 0;
uint32_t accent_colors[4] = { 0x2563EB, 0x16A34A, 0xEA580C, 0x9333EA };
int accent_index = 0;

bool start_menu_open = false;
uint32_t last_scancode = 0;

void draw_window(const Window& win, bool active) {
    if (win.closed) return;

    // Тень
    draw_shadow(win.x + 6, win.y + 6, win.w, win.h);

    // Тело окна
    fill_rect(win.x, win.y, win.w, win.h, 0xF8FAFC);
    draw_rect(win.x, win.y, win.w, win.h, 0x64748B);

    // Заголовок окна
    uint32_t title_c1 = active ? accent_colors[accent_index] : 0x64748B;
    uint32_t title_c2 = active ? 0x1E3A8A : 0x334155;
    draw_gradient_v(win.x + 1, win.y + 1, win.w - 2, 24, title_c1, title_c2);
    draw_string(win.x + 8, win.y + 7, win.title, 0xFFFFFF);

    // Кнопка закрытия [X]
    int btn_x = win.x + win.w - 20;
    int btn_y = win.y + 4;
    fill_rect(btn_x, btn_y, 16, 16, 0xEF4444);
    draw_string(btn_x + 4, btn_y + 4, "X", 0xFFFFFF);

    // Контент окна в зависимости от ID
    int cx = win.x + 12;
    int cy = win.y + 36;

    if (win.id == 0) {
        draw_string(cx, cy, "LiteOS Kernel v1.0", 0x0F172A); cy += 18;
        draw_string(cx, cy, "Architecture: x86 (32-bit Protected Mode)", 0x334155); cy += 16;
        draw_string(cx, cy, "Video: VBE 1024x768 @ 32bpp (Linear FB)", 0x334155); cy += 16;
        draw_string(cx, cy, "Bootloader: GNU GRUB (Multiboot 1)", 0x334155); cy += 16;
        draw_string(cx, cy, "C++ Standard: Freestanding (No Libc)", 0x334155); cy += 24;
        fill_rect(cx, cy, win.w - 24, 2, 0xCBD5E1); cy += 12;
        draw_string(cx, cy, "Status: Nanokernel is running smoothly!", 0x16A34A);
    }
    else if (win.id == 1) {
        draw_string(cx, cy, "Test interactive GUI components:", 0x0F172A); cy += 22;

        // Кнопка 1: Счётчик
        fill_rect(cx, cy, 120, 26, 0x2563EB);
        draw_rect(cx, cy, 120, 26, 0x1E40AF);
        draw_string(cx + 12, cy + 8, "[ Click Me! ]", 0xFFFFFF);

        char c_buf[16];
        int_to_str(click_counter, c_buf);
        draw_string(cx + 130, cy + 8, "Clicks: ", 0x0F172A);
        draw_string(cx + 190, cy + 8, c_buf, 0xDC2626);
        cy += 38;

        // Кнопка 2: Смена цвета темы
        fill_rect(cx, cy, 140, 26, 0xE2E8F0);
        draw_rect(cx, cy, 140, 26, 0x94A3B8);
        draw_string(cx + 10, cy + 8, "Change Color", 0x0F172A);
        fill_rect(cx + 150, cy + 3, 20, 20, accent_colors[accent_index]);
        draw_rect(cx + 150, cy + 3, 20, 20, 0x000000);
        cy += 40;

        // Кнопка 3: Восстановление всех окон
        fill_rect(cx, cy, 150, 26, 0xE2E8F0);
        draw_rect(cx, cy, 150, 26, 0x94A3B8);
        draw_string(cx + 10, cy + 8, "Restore Windows", 0x0F172A);
        cy += 36;

        draw_string(cx, cy, "* Drag titlebars to move windows", 0x64748B);
    }
    else if (win.id == 2) {
        draw_string(cx, cy, "Realtime System & Input Monitor:", 0x0F172A); cy += 20;

        // Координаты мыши
        char mx_buf[16], my_buf[16];
        int_to_str(mouse_x, mx_buf);
        int_to_str(mouse_y, my_buf);
        draw_string(cx, cy, "Mouse Position: X=", 0x334155);
        draw_string(cx + 144, cy, mx_buf, 0x2563EB);
        draw_string(cx + 184, cy, " Y=", 0x334155);
        draw_string(cx + 216, cy, my_buf, 0x2563EB);
        cy += 16;

        // Кнопки мыши
        draw_string(cx, cy, "Buttons: ", 0x334155);
        draw_string(cx + 70, cy, mouse_btn_left ? "[LEFT DOWN]" : "[LEFT UP]", mouse_btn_left ? 0xDC2626 : 0x64748B);
        draw_string(cx + 170, cy, mouse_btn_right ? "[RIGHT DOWN]" : "[RIGHT UP]", mouse_btn_right ? 0xDC2626 : 0x64748B);
        cy += 16;

        // Клавиатура
        char sc_buf[16];
        hex_to_str(last_scancode, sc_buf);
        draw_string(cx, cy, "Last PS/2 Scancode: ", 0x334155);
        draw_string(cx + 160, cy, sc_buf, 0xD97706);
        cy += 24;

        fill_rect(cx, cy, win.w - 24, 30, 0x0F172A);
        draw_string(cx + 8, cy + 10, "> Tip: Use Arrow keys + Enter to navigate without mouse", 0x38BDF8);
    }
}

// --- ДРАЙВЕР PS/2 МЫШИ (POLLING) ---
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
    outb(0x64, 0xA8); // Включение AUX порта

    mouse_wait_write();
    outb(0x64, 0x20); // Чтение байта конфигурации
    uint8_t status = mouse_read();

    status &= ~(1 << 5); // Включить тактирование мыши
    status &= ~(1 << 1); // Без прерываний (работаем через опрос)
    status &= ~(1 << 0);

    mouse_wait_write();
    outb(0x64, 0x60);
    mouse_wait_write();
    outb(0x60, status);

    mouse_write(0xF6); // Загрузка дефолтных настроек
    mouse_read();      // ACK

    mouse_write(0xF4); // Включение передачи пакетов
    mouse_read();      // ACK
}

void process_inputs() {
    static int cycle = 0;
    static uint8_t packet[3];

    while (true) {
        uint8_t st = inb(0x64);
        if (!(st & 1)) break;

        uint8_t data = inb(0x60);

        if (st & 0x20) {
            // Данные от мыши
            switch (cycle) {
                case 0:
                    if (data & 0x08) { // Бит 3 всегда равен 1
                        packet[0] = data;
                        cycle = 1;
                    }
                    break;
                case 1:
                    packet[1] = data;
                    cycle = 2;
                    break;
                case 2:
                    packet[2] = data;
                    cycle = 0;

                    mouse_btn_left = (packet[0] & 1);
                    mouse_btn_right = (packet[0] & 2);

                    int dx = packet[1];
                    if (packet[0] & 0x10) dx -= 256;
                    int dy = packet[2];
                    if (packet[0] & 0x20) dy -= 256;

                    mouse_x += dx;
                    mouse_y -= dy; // Инвертируем Y для экрана

                    if (mouse_x < 0) mouse_x = 0;
                    if (mouse_x >= SCREEN_W) mouse_x = SCREEN_W - 1;
                    if (mouse_y < 0) mouse_y = 0;
                    if (mouse_y >= SCREEN_H) mouse_y = SCREEN_H - 1;
                    break;
            }
        } else {
            // Данные от клавиатуры
            last_scancode = data;

            // Поддержка управления курсором со стрелок
            if (data == 0x48) mouse_y -= 10;      // Up
            if (data == 0x50) mouse_y += 10;      // Down
            if (data == 0x4B) mouse_x -= 10;      // Left
            if (data == 0x4D) mouse_x += 10;      // Right
            if (data == 0x1C) mouse_btn_left = true;  // Enter down
            if (data == 0x9C) mouse_btn_left = false; // Enter up

            if (mouse_x < 0) mouse_x = 0;
            if (mouse_x >= SCREEN_W) mouse_x = SCREEN_W - 1;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_y >= SCREEN_H) mouse_y = SCREEN_H - 1;
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

    mouse_init();

    bool prev_lclick = false;

    // Главный цикл ОС
    while (true) {
        process_inputs();

        bool lclick_pressed = (!prev_lclick && mouse_btn_left);
        bool lclick_released = (prev_lclick && !mouse_btn_left);
        prev_lclick = mouse_btn_left;

        // Обработка перетаскивания и кликов
        if (lclick_pressed) {
            // Клик по меню Пуск
            if (mouse_x >= 4 && mouse_x <= 84 && mouse_y >= SCREEN_H - 28 && mouse_y <= SCREEN_H - 4) {
                start_menu_open = !start_menu_open;
            }
            // Клик внутри открытого меню Пуск
            else if (start_menu_open && mouse_x >= 4 && mouse_x <= 164 && mouse_y >= SCREEN_H - 120 && mouse_y < SCREEN_H - 32) {
                if (mouse_y >= SCREEN_H - 60) {
                    // Перезагрузка ПК через сброс 8042 контроллера
                    outb(0x64, 0xFE);
                } else {
                    for (int i = 0; i < 3; i++) windows[i].closed = false;
                    start_menu_open = false;
                }
            } else {
                start_menu_open = false;

                // Проверка кликов по окнам от верхнего к нижнему
                bool handled = false;
                for (int i = 2; i >= 0; i--) {
                    int w_idx = z_order[i];
                    Window& win = windows[w_idx];
                    if (win.closed) continue;

                    if (mouse_x >= win.x && mouse_x < win.x + win.w &&
                        mouse_y >= win.y && mouse_y < win.y + win.h) {

                        // Перемещаем окно наверх
                        for (int k = i; k < 2; k++) {
                            z_order[k] = z_order[k + 1];
                        }
                        z_order[2] = w_idx;

                        // Кнопка [X]
                        if (mouse_x >= win.x + win.w - 20 && mouse_x <= win.x + win.w - 4 &&
                            mouse_y >= win.y + 4 && mouse_y <= win.y + 20) {
                            win.closed = true;
                        }
                        // Заголовок (начало перетаскивания)
                        else if (mouse_y <= win.y + 24) {
                            dragging_window = w_idx;
                            drag_offset_x = mouse_x - win.x;
                            drag_offset_y = mouse_y - win.y;
                        }
                        // Интерактивные кнопки окна Control Panel
                        else if (win.id == 1) {
                            int cx = win.x + 12;
                            int cy = win.y + 58;

                            if (mouse_x >= cx && mouse_x <= cx + 120 && mouse_y >= cy && mouse_y <= cy + 26) {
                                click_counter++;
                            }
                            cy += 38;
                            if (mouse_x >= cx && mouse_x <= cx + 140 && mouse_y >= cy && mouse_y <= cy + 26) {
                                accent_index = (accent_index + 1) % 4;
                            }
                            cy += 40;
                            if (mouse_x >= cx && mouse_x <= cx + 150 && mouse_y >= cy && mouse_y <= cy + 26) {
                                for (int k = 0; k < 3; k++) windows[k].closed = false;
                            }
                        }
                        handled = true;
                        break;
                    }
                }
                if (!handled) {
                    dragging_window = -1;
                }
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
        // 1. Градиент рабочего стола (современный тёмно-синий фон)
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

        // Вкладки окон на панели задач
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

        // Часы реального времени (RTC)
        int h = 0, m = 0, s = 0;
        get_rtc_time(h, m, s);
        char time_buf[16];
        time_buf[0] = '0' + (h / 10);
        time_buf[1] = '0' + (h % 10);
        time_buf[2] = ':';
        time_buf[3] = '0' + (m / 10);
        time_buf[4] = '0' + (m % 10);
        time_buf[5] = ':';
        time_buf[6] = '0' + (s / 10);
        time_buf[7] = '0' + (s % 10);
        time_buf[8] = '\0';
        draw_string(SCREEN_W - 80, SCREEN_H - 20, time_buf, 0x38BDF8);

        // 4. Меню «Пуск» (если открыто)
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

        // 5. Курсор мыши поверх всего
        draw_cursor(mouse_x, mouse_y);

        // 6. Сброс кадра в VRAM (быстрое копирование строк)
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