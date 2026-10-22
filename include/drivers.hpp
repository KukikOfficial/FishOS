#pragma once
#include <stdint.h>

// CMOS RTC
void get_rtc_time(int& h, int& m, int& s);

// ATA IDE Hard Disk
extern bool ata_present;
void ata_read_sector(uint32_t lba, uint8_t* buffer);
void ata_write_sector(uint32_t lba, const uint8_t* buffer);

// Special Keyboard keys
enum SpecialKey {
    KEY_NONE = 0,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_UP,
    KEY_DOWN,
    KEY_HOME,
    KEY_END,
    KEY_DELETE,
    KEY_TAB,
    KEY_SAVE
};

// PS/2 Keyboard & Mouse
extern int mouse_x;
extern int mouse_y;
extern bool mouse_btn_left;
extern bool mouse_btn_right;
extern bool shift_pressed;
extern bool ctrl_pressed;
extern bool alt_pressed;

void mouse_init();
void process_inputs();

// Audio Feedback
void audio_click();
void audio_beep(uint32_t freq, uint32_t ms);