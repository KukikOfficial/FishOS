// FishOS application toolkit: windows, events, clipboard and shared widgets.
#pragma once
#include <fihcore/abi.h>
#include "fishos/gfx.h"
#include "fishos/wsproto.h"

namespace ui {
using gfx::Rect;

namespace theme {
constexpr uint32_t bg = 0xF4F6F7;
constexpr uint32_t panel = 0xE7ECEE;
constexpr uint32_t border = 0xC6D0D5;
constexpr uint32_t text = 0x1B2A32;
constexpr uint32_t text_dim = 0x63737C;
constexpr uint32_t field = 0xFFFFFF;
constexpr uint32_t accent = 0x1C6A80;      // deep teal
constexpr uint32_t accent_dark = 0x15505F;
constexpr uint32_t accent_text = 0xFFFFFF;
constexpr uint32_t select = 0xCFE5EC;      // selection, unfocused
constexpr uint32_t select_strong = 0x2A86A0; // selection, focused
constexpr uint32_t coral = 0xEE7B57;       // focus ring / highlights
constexpr uint32_t danger = 0xC0423C;
constexpr int row_h = 24;
constexpr int toolbar_h = 40;
constexpr int status_h = 24;
}

struct Event {
    enum Type { None, Key, KeyUp, MouseDown, MouseUp, MouseMove, Wheel, Close, Resize, FocusIn, FocusOut };
    Type type = None;
    int x = 0, y = 0;     // window-local coordinates
    int wheel = 0;        // positive = towards the user (scroll down)
    int clicks = 0;       // 2 for a double click
    int w = 0, h = 0;     // Resize
    uint32_t key = 0, ch = 0, mods = 0, buttons = 0;
    bool ctrl() const { return mods & FIH_MOD_CTRL; }
    bool shift() const { return mods & FIH_MOD_SHIFT; }
    bool alt() const { return mods & FIH_MOD_ALT; }
};

class Window {
public:
    int id = 0;
    int w = 0, h = 0;
    bool focused = false;
    gfx::Canvas canvas;

    bool open(const char* title, int width, int height, uint32_t flags = WS_FLAG_RESIZABLE);
    void close();
    void present();
    void present(Rect r);
    void set_title(const char* title);
    void activate();

    // Internal: replaces the pixel buffer (used for resizing).
    bool alloc_buffer(int width, int height);
    int shm = 0;
};

// Connects to the display server. Returns false if no desktop is running.
bool connect();
// Next event for any open window; false on timeout (timeout_ms < 0 waits forever).
// Child-exit notifications are handled here (children are reaped).
bool next_event(Window** win, Event* ev, int timeout_ms);

bool clipboard_set(const char* text, int len);
char* clipboard_get(int* len); // malloc'd and NUL-terminated; nullptr when empty

// Starts a program without stdio redirection; returns pid or -error.
int launch(const char* path, const char* arg = nullptr);
const char* error_string(int err);

// ---- drawing helpers -------------------------------------------------------
enum ButtonStyle { Normal, Primary, Danger, Flat };
void draw_button(gfx::Canvas& c, Rect r, const char* label, ButtonStyle style = Normal, bool pressed = false,
                 bool disabled = false);
void draw_focus_ring(gfx::Canvas& c, Rect r);

enum class Icon { Folder, File, Text, Program, Files, Editor, Terminal, Monitor, Up, Back, NewFolder, NewFile };
void draw_icon(gfx::Canvas& c, Icon icon, int x, int y, int size);

// ---- widgets ---------------------------------------------------------------

// Single-line text input with selection and clipboard support.
struct LineEdit {
    static constexpr int MAX = 255;
    char text[MAX + 1] = {};
    int len = 0, cursor = 0, anchor = -1, scroll = 0;

    void set(const char* s);
    void select_all();
    bool key(const Event& e); // true if the event was consumed
    void click(const Event& e, Rect r);
    void draw(gfx::Canvas& c, Rect r, bool focused);
    bool has_selection() const { return anchor >= 0 && anchor != cursor; }
    void delete_selection();
};

// Modal message, confirmation or text prompt drawn inside a window.
struct Dialog {
    enum Kind { None, Message, Confirm, Prompt };
    Kind kind = None;
    int tag = 0;     // caller-defined purpose
    int result = -1; // index of the chosen button; cancel_index on Esc
    char title[64] = {};
    char message[256] = {};
    const char* buttons[3] = {};
    int nbuttons = 0, cancel_index = 0, pressed = -1;
    LineEdit input;
    Rect box;

    bool active() const { return kind != None; }
    void message_box(int tag, const char* title, const char* msg);
    void confirm(int tag, const char* title, const char* msg, const char* yes, const char* no,
                 const char* third = nullptr);
    void prompt(int tag, const char* title, const char* msg, const char* initial, const char* ok);
    // Returns true when the dialog finished on this event (result is set).
    bool handle(const Event& e);
    void draw(gfx::Canvas& c);
    void close() { kind = None; }
};

// Scrollable list state; rows are drawn by the caller.
struct ListView {
    Rect area;
    int count = 0, selected = -1, top = 0, row_h = theme::row_h;
    bool dragging_thumb = false;
    int drag_offset = 0;

    int visible() const { return area.h / row_h > 0 ? area.h / row_h : 1; }
    bool key(const Event& e);        // navigation keys; true if selection changed
    int hit(int x, int y) const;     // row index or -1
    bool mouse(const Event& e);      // wheel and scrollbar; true if consumed
    void ensure_visible();
    void clamp();
    Rect row_rect(int index) const;  // area-relative row rectangle (may be off-screen)
    void draw_scrollbar(gfx::Canvas& c) const;
};

void format_size(char* out, int n, uint32_t bytes);
void format_time(char* out, int n, uint32_t epoch2000);

} // namespace ui
