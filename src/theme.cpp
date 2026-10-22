#include "theme.hpp"

ThemeId current_theme_id = THEME_DARK;
ThemeColors current_theme;

static const ThemeColors themes[3] = {
    // THEME_DARK
    {
        0x0F172A, 0x1E293B, 0x334155, // Desktop
        0xF8FAFC, 0x475569,           // Window bg, border
        0x2563EB, 0x1E40AF,           // Title active gradient
        0x64748B, 0x334155,           // Title inactive gradient
        0xFFFFFF,                     // Title text
        0x0F172A, 0x334155,           // Taskbar
        0x2563EB, 0x38BDF8,           // Start button
        0x0F172A, 0x64748B, 0x38BDF8, // Text colors
        false                         // 3D bevel
    },
    // THEME_WIN95
    {
        0x008080, 0x008080, 0x004040, // Classic Teal Desktop (#008080)
        0xC0C0C0, 0x000000,           // Win95 Grey Window
        0x000080, 0x1084D0,           // Win95 Dark Blue Gradient Title
        0x808080, 0xA0A0A0,           // Inactive Grey Title
        0xFFFFFF,                     // Title text
        0xC0C0C0, 0x808080,           // Classic Grey Taskbar
        0xC0C0C0, 0x000000,           // Start button
        0x000000, 0x555555, 0x000080, // Text colors
        true                          // 3D bevel
    },
    // THEME_MATRIX
    {
        0x000000, 0x051505, 0x14532D, // Deep Green/Black Desktop
        0x050D05, 0x16A34A,           // Matrix Dark Window
        0x16A34A, 0x14532D,           // Matrix Green Title
        0x052E16, 0x02170B,           // Inactive Dark Title
        0x22C55E,                     // Title text
        0x020803, 0x16A34A,           // Taskbar
        0x14532D, 0x22C55E,           // Start button
        0x22C55E, 0x16A34A, 0x4ADE80, // Text colors
        false                         // 3D bevel
    }
};

void theme_init() {
    theme_set(THEME_DARK);
}

void theme_set(ThemeId id) {
    if (id > THEME_MATRIX) id = THEME_DARK;
    current_theme_id = id;
    current_theme = themes[id];
}
