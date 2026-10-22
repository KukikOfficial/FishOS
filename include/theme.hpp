#pragma once
#include <stdint.h>

enum ThemeId {
    THEME_DARK = 0,
    THEME_WIN95 = 1,
    THEME_MATRIX = 2
};

struct ThemeColors {
    uint32_t bg_top;
    uint32_t bg_bottom;
    uint32_t grid_dots;
    uint32_t win_bg;
    uint32_t win_border;
    uint32_t title_active_c1;
    uint32_t title_active_c2;
    uint32_t title_inactive_c1;
    uint32_t title_inactive_c2;
    uint32_t title_text;
    uint32_t taskbar_bg;
    uint32_t taskbar_border;
    uint32_t start_btn_bg;
    uint32_t start_btn_border;
    uint32_t text_primary;
    uint32_t text_secondary;
    uint32_t text_accent;
    bool is_3d_bevel;
};

extern ThemeId current_theme_id;
extern ThemeColors current_theme;

void theme_init();
void theme_set(ThemeId id);
