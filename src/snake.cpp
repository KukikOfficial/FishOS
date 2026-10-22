#include "snake.hpp"
#include "graphics.hpp"
#include "vfs.hpp"
#include "string.hpp"
#include "drivers.hpp"
#include "timer.hpp"

constexpr int GRID_W = 24;
constexpr int GRID_H = 18;
constexpr int CELL_SIZE = 12;

struct Point { int x, y; };

static Point snake_body[GRID_W * GRID_H];
static int snake_length = 4;
static int dir_x = 1, dir_y = 0;
static Point food = { 10, 8 };
static int score = 0;
static int high_score = 0;
static bool game_over = false;
static uint32_t last_move_tick = 0;

static void spawn_food() {
    static uint32_t seed = 12345;
    seed = (seed * 1103515245 + 12345) & 0x7fffffff;
    food.x = seed % GRID_W;
    food.y = (seed / GRID_W) % GRID_H;
}

static void load_high_score() {
    vfs_node_t* n = vfs_lookup("/system/snake.score");
    if (n && n->content) {
        int val = 0;
        for (int i = 0; n->content[i] >= '0' && n->content[i] <= '9'; i++) {
            val = val * 10 + (n->content[i] - '0');
        }
        high_score = val;
    }
}

static void save_high_score() {
    char buf[16];
    int_to_str(high_score, buf);
    vfs_write_file("/system/snake.score", buf, kstrlen(buf));
}

void snake_init() {
    snake_length = 4;
    dir_x = 1; dir_y = 0;
    snake_body[0] = { 6, 8 };
    snake_body[1] = { 5, 8 };
    snake_body[2] = { 4, 8 };
    snake_body[3] = { 3, 8 };
    score = 0;
    game_over = false;
    spawn_food();
    load_high_score();
}

void snake_handle_key(SpecialKey key) {
    if (game_over) {
        snake_init();
        return;
    }

    switch (key) {
        case KEY_LEFT:
            if (dir_x != 1) { dir_x = -1; dir_y = 0; }
            break;
        case KEY_RIGHT:
            if (dir_x != -1) { dir_x = 1; dir_y = 0; }
            break;
        case KEY_UP:
            if (dir_y != 1) { dir_x = 0; dir_y = -1; }
            break;
        case KEY_DOWN:
            if (dir_y != -1) { dir_x = 0; dir_y = 1; }
            break;
        default: break;
    }
}

void snake_update() {
    if (game_over) return;

    uint32_t now = get_tick_count();
    // Step every 10 ticks (100ms)
    if (now - last_move_tick < 10) return;
    last_move_tick = now;

    // Calculate new head position
    Point new_head = { snake_body[0].x + dir_x, snake_body[0].y + dir_y };

    // Wall collision
    if (new_head.x < 0 || new_head.x >= GRID_W || new_head.y < 0 || new_head.y >= GRID_H) {
        game_over = true;
        audio_beep(300, 150);
        return;
    }

    // Self collision
    for (int i = 0; i < snake_length; i++) {
        if (new_head.x == snake_body[i].x && new_head.y == snake_body[i].y) {
            game_over = true;
            audio_beep(300, 150);
            return;
        }
    }

    // Move body
    for (int i = snake_length; i > 0; i--) {
        snake_body[i] = snake_body[i - 1];
    }
    snake_body[0] = new_head;

    // Check food eaten
    if (new_head.x == food.x && new_head.y == food.y) {
        snake_length++;
        score += 10;
        audio_beep(1200, 30);
        if (score > high_score) {
            high_score = score;
            save_high_score();
        }
        spawn_food();
    }
}

void snake_draw(int x, int y, int w, int h) {
    (void)w; (void)h;
    snake_update();

    int start_x = x + 12;
    int start_y = y + 36;
    int board_w = GRID_W * CELL_SIZE;
    int board_h = GRID_H * CELL_SIZE;

    // Board background & border
    fill_rect(start_x, start_y, board_w, board_h, 0x05101A);
    draw_rect(start_x, start_y, board_w, board_h, 0x22C55E);

    // Score Board
    char sc_buf[32] = "Score: ";
    char num_buf[16];
    int_to_str(score, num_buf);
    int p = kstrlen(sc_buf);
    for (int s = 0; num_buf[s]; s++) sc_buf[p++] = num_buf[s];
    sc_buf[p++] = ' '; sc_buf[p++] = '|'; sc_buf[p++] = ' '; sc_buf[p++] = 'B'; sc_buf[p++] = 'e'; sc_buf[p++] = 's'; sc_buf[p++] = 't'; sc_buf[p++] = ':'; sc_buf[p++] = ' ';
    int_to_str(high_score, num_buf);
    for (int s = 0; num_buf[s]; s++) sc_buf[p++] = num_buf[s];
    sc_buf[p] = '\0';
    draw_string(start_x, y + 14, sc_buf, 0x38BDF8);

    // Draw Food
    int fx = start_x + food.x * CELL_SIZE;
    int fy = start_y + food.y * CELL_SIZE;
    fill_rect(fx + 1, fy + 1, CELL_SIZE - 2, CELL_SIZE - 2, 0xEF4444);
    draw_rect(fx + 1, fy + 1, CELL_SIZE - 2, CELL_SIZE - 2, 0xFCA5A5);

    // Draw Snake Body
    for (int i = 0; i < snake_length; i++) {
        int bx = start_x + snake_body[i].x * CELL_SIZE;
        int by = start_y + snake_body[i].y * CELL_SIZE;
        fill_rect(bx + 1, by + 1, CELL_SIZE - 2, CELL_SIZE - 2, (i == 0) ? 0x4ADE80 : 0x22C55E);
        draw_rect(bx + 1, by + 1, CELL_SIZE - 2, CELL_SIZE - 2, (i == 0) ? 0xFFFFFF : 0x15803D);
    }

    if (game_over) {
        int gw = 200, gh = 60;
        int gx = start_x + (board_w - gw) / 2;
        int gy = start_y + (board_h - gh) / 2;
        draw_shadow(gx + 4, gy + 4, gw, gh);
        fill_rect(gx, gy, gw, gh, 0x7F1D1D);
        draw_rect(gx, gy, gw, gh, 0xEF4444);
        draw_string(gx + 40, gy + 14, "GAME OVER!", 0xFFFFFF);
        draw_string(gx + 16, gy + 34, "Press Arrow to Restart", 0xFCA5A5);
    }
}
