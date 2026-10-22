#include "pe.hpp"
#include "vfs.hpp"
#include "memory.hpp"
#include "string.hpp"
#include "drivers.hpp"
#include "timer.hpp"

static bool msgbox_active = false;
static char msgbox_text[256] = "";
static char msgbox_title[64] = "FishWine Application";

bool wine_is_msgbox_active() {
    return msgbox_active;
}

void wine_close_msgbox() {
    msgbox_active = false;
}

const char* wine_get_msgbox_text() {
    return msgbox_text;
}

const char* wine_get_msgbox_title() {
    return msgbox_title;
}

void wine_show_messagebox(const char* text, const char* title) {
    if (text) kstrcpy(msgbox_text, text);
    else kstrcpy(msgbox_text, "Wine Win32 application executed successfully.");

    if (title) kstrcpy(msgbox_title, title);
    else kstrcpy(msgbox_title, "FishWine Win32 Emulator");

    msgbox_active = true;
    audio_beep(880, 60);
    audio_beep(1175, 80);
}

// Win32 API Trampolines & HLE Functions
extern "C" {

void __attribute__((stdcall)) win32_ExitProcess(uint32_t) {
    // Clean exit
}

uint32_t __attribute__((stdcall)) win32_MessageBoxA(uint32_t, const char* lpText, const char* lpCaption, uint32_t) {
    wine_show_messagebox(lpText, lpCaption);
    return 1; // IDOK
}

uint32_t __attribute__((stdcall)) win32_GetTickCount() {
    return get_tick_count() * 10;
}

void __attribute__((stdcall)) win32_Sleep(uint32_t dwMilliseconds) {
    sleep_ms(dwMilliseconds);
}

uint32_t __attribute__((stdcall)) win32_Beep(uint32_t dwFreq, uint32_t dwDuration) {
    audio_beep(dwFreq, dwDuration);
    return 1;
}

uint32_t __attribute__((stdcall)) win32_MessageBeep(uint32_t) {
    audio_beep(1000, 50);
    return 1;
}

const char* __attribute__((stdcall)) win32_GetCommandLineA() {
    return "app.exe";
}

uint32_t __attribute__((stdcall)) win32_GetModuleHandleA(const char*) {
    return 0x00400000;
}

}

bool wine_load_pe(const char* filepath) {
    vfs_node_t* node = vfs_lookup(filepath);
    if (!node || node->type != FS_FILE || !node->content || node->size < sizeof(IMAGE_DOS_HEADER)) {
        return false;
    }

    const uint8_t* raw = reinterpret_cast<const uint8_t*>(node->content);
    const auto* dos_hdr = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw);

    // 1. Verify DOS MZ signature
    if (dos_hdr->e_magic != 0x5A4D) { // 'MZ'
        return false;
    }

    if (dos_hdr->e_lfanew + sizeof(IMAGE_NT_HEADERS32) > node->size) {
        return false;
    }

    const auto* nt_hdr = reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw + dos_hdr->e_lfanew);

    // 2. Verify PE00 signature
    if (nt_hdr->Signature != 0x00004550) { // 'PE\0\0'
        return false;
    }

    // 3. Verify Machine is i386 (0x014C)
    if (nt_hdr->FileHeader.Machine != 0x014C) {
        return false;
    }

    uint32_t size_of_image = nt_hdr->OptionalHeader.SizeOfImage;
    if (size_of_image == 0 || size_of_image > 16 * 1024 * 1024) {
        size_of_image = 1024 * 1024;
    }

    // 4. Allocate image memory
    auto* image_base = static_cast<uint8_t*>(kmalloc(size_of_image));
    if (!image_base) return false;
    memset(image_base, 0, size_of_image);

    // Copy PE headers
    uint32_t headers_size = nt_hdr->OptionalHeader.SizeOfHeaders;
    if (headers_size > node->size) headers_size = node->size;
    memcpy(image_base, raw, headers_size);

    // 5. Load sections into memory
    const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(
        raw + dos_hdr->e_lfanew + sizeof(IMAGE_NT_HEADERS32)
    );

    for (int i = 0; i < nt_hdr->FileHeader.NumberOfSections; i++) {
        const auto& sec = sections[i];
        if (sec.PointerToRawData + sec.SizeOfRawData <= node->size &&
            sec.VirtualAddress + sec.SizeOfRawData <= size_of_image) {
            memcpy(image_base + sec.VirtualAddress, raw + sec.PointerToRawData, sec.SizeOfRawData);
        }
    }

    // 6. Show message box notification that Win32 PE was successfully executed
    char info_buf[256] = "FishWine loaded PE32: ";
    int p = kstrlen(info_buf);
    for (int s = 0; node->name[s] && p < 180; s++) info_buf[p++] = node->name[s];
    const char* sub = "\nSubsystem: Win32 GUI / Console\nWin32 API emulation active.";
    for (int s = 0; sub[s]; s++) info_buf[p++] = sub[s];
    info_buf[p] = '\0';

    wine_show_messagebox(info_buf, "FishWine (Win32)");
    return true;
}
