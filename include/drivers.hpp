#pragma once
#include <stdint.h>

// CMOS RTC
void get_rtc_time(int& h, int& m, int& s);

// ATA IDE Hard Disk
extern bool ata_present;
void ata_read_sector(uint32_t lba, uint8_t* buffer);
void ata_write_sector(uint32_t lba, const uint8_t* buffer);

// PS/2 Keyboard & Mouse
extern int mouse_x;
extern int mouse_y;
extern bool mouse_btn_left;
extern bool shift_pressed;

void mouse_init();
void process_inputs();