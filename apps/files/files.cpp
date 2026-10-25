// FishOS Files: browse and manage the filesystem.
#include <fihcore/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fishos/fs.h"
#include "fishos/ui.h"

using gfx::Rect;
using gfx::font_sans;
using ui::Event;
namespace theme = ui::theme;

namespace {
struct Entry {
    char name[FIH_NAME_MAX + 3];
    uint32_t type, size, mtime;
};

ui::Window win;
ui::Dialog dialog;
ui::ListView list;
ui::LineEdit location;
bool location_focused;
Entry* entries;
int nentries;
char cwd[FIH_PATH_MAX] = "/";
char history[16][FIH_PATH_MAX];
int nhistory;
char status[160];
bool readonly_dir;

// Internal clipboard for copy/cut of files.
char clip_path[FIH_PATH_MAX];
bool clip_cut;

int pressed = -1;
enum { DLG_ERROR = 1, DLG_NEW_FOLDER, DLG_NEW_FILE, DLG_RENAME, DLG_DELETE };

enum Tool { T_BACK, T_UP, T_NEW_FOLDER, T_NEW_FILE, T_RENAME, T_DELETE, T_COPY, T_CUT, T_PASTE, T_COUNT };
const char* const TOOL_LABEL[T_COUNT] = {"", "", "", "", "Rename", "Delete", "Copy", "Cut", "Paste"};

Rect tool_rect(int i) {
    if (i < 4) return {8 + i * 36, 6, 32, 28};
    return {8 + 4 * 36 + 8 + (i - 4) * 70, 6, 66, 28};
}
Rect location_rect() {
    int x = tool_rect(T_COUNT - 1).x + 74;
    return {x, 6, win.w - x - 8, 28};
}
Rect header_rect() { return {0, theme::toolbar_h, win.w, 24}; }
Rect list_rect() { return {0, theme::toolbar_h + 24, win.w, win.h - theme::toolbar_h - 24 - theme::status_h}; }

int cmp_entries(const void* a, const void* b) {
    const Entry* x = static_cast<const Entry*>(a);
    const Entry* y = static_cast<const Entry*>(b);
    if (x->type != y->type) return x->type == FIH_FT_DIR ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

void set_status(const char* s) { strlcpy(status, s, sizeof status); }

int load_dir(const char* path) {
    int fd = fih_open(path, FIH_O_READ);
    if (fd < 0) return fd;
    fih_stat_t st;
    if (fih_fstat(fd, &st) == 0 && st.type != FIH_FT_DIR) {
        fih_close(fd);
        return -FIH_ENOTDIR;
    }
    int cap = 64, n = 0;
    Entry* list_new = static_cast<Entry*>(malloc(sizeof(Entry) * (size_t)cap));
    fih_dirent_t d;
    for (uint32_t i = 0; list_new && fih_readdir(fd, i, &d) == 1; i++) {
        if (d.name[0] == '.') continue; // hidden: temporary save files and similar
        if (n == cap) {
            cap *= 2;
            Entry* grown = static_cast<Entry*>(realloc(list_new, sizeof(Entry) * (size_t)cap));
            if (!grown) break;
            list_new = grown;
        }
        Entry& e = list_new[n++];
        strlcpy(e.name, d.name, sizeof e.name);
        e.type = d.type;
        e.size = d.size;
        char full[FIH_PATH_MAX];
        fs::join(path, d.name, full, sizeof full);
        fih_stat_t cs;
        e.mtime = fih_stat(full, &cs) == 0 ? cs.mtime : 0;
    }
    fih_close(fd);
    if (!list_new) return -FIH_ENOMEM;
    qsort(list_new, (size_t)n, sizeof(Entry), cmp_entries);
    free(entries);
    entries = list_new;
    nentries = n;
    readonly_dir = st.flags & FIH_ST_READONLY;
    return 0;
}

void refresh(const char* select_name = nullptr) {
    char keep[FIH_NAME_MAX + 3] = "";
    if (select_name) strlcpy(keep, select_name, sizeof keep);
    else if (list.selected >= 0 && list.selected < nentries) strlcpy(keep, entries[list.selected].name, sizeof keep);
    int e = load_dir(cwd);
    if (e) {
        char msg[200];
        snprintf(msg, sizeof msg, "Cannot read %s: %s", cwd, ui::error_string(e));
        set_status(msg);
    }
    list.count = nentries;
    list.selected = -1;
    for (int i = 0; i < nentries; i++) if (!strcmp(entries[i].name, keep)) list.selected = i;
    if (list.selected < 0 && nentries) list.selected = 0;
    list.ensure_visible();
    location.set(cwd);
}

void update_title() {
    char t[80];
    snprintf(t, sizeof t, "%s - Files", strcmp(cwd, "/") ? fs::basename(cwd) : "/");
    win.set_title(t);
}

void navigate(const char* path, bool remember = true) {
    char abs[FIH_PATH_MAX];
    int e = fs::absolute(path, abs, sizeof abs);
    if (!e && !fs::is_dir(abs)) e = fs::exists(abs) ? -FIH_ENOTDIR : -FIH_ENOENT;
    if (e) {
        char msg[200];
        snprintf(msg, sizeof msg, "%s: %s", path, ui::error_string(e));
        dialog.message_box(DLG_ERROR, "Files", msg);
        location.set(cwd);
        return;
    }
    if (remember && strcmp(abs, cwd) != 0) {
        if (nhistory == 16) { memmove(history[0], history[1], sizeof history[0] * 15); nhistory--; }
        strlcpy(history[nhistory++], cwd, sizeof history[0]);
    }
    // Select the folder we came from when going up.
    char from[FIH_NAME_MAX + 3] = "";
    size_t n = strlen(abs);
    if (n == 1 && cwd[1]) strlcpy(from, cwd + 1, sizeof from);
    else if (strncmp(cwd, abs, n) == 0 && cwd[n] == '/') strlcpy(from, cwd + n + 1, sizeof from);
    if (char* slash = strchr(from, '/')) *slash = 0;
    strlcpy(cwd, abs, sizeof cwd);
    list.top = 0;
    status[0] = 0;
    refresh(from[0] ? from : "");
    update_title();
}

Entry* selected() { return list.selected >= 0 && list.selected < nentries ? &entries[list.selected] : nullptr; }

void selected_path(char* out, size_t n) { fs::join(cwd, selected()->name, out, n); }

void report(const char* what, int e) {
    char msg[220];
    snprintf(msg, sizeof msg, "%s: %s.", what, ui::error_string(e));
    dialog.message_box(DLG_ERROR, "Files", msg);
}

void open_selected() {
    Entry* e = selected();
    if (!e) return;
    char p[FIH_PATH_MAX];
    selected_path(p, sizeof p);
    if (e->type == FIH_FT_DIR) return navigate(p);
    // Programs in /system/bin run directly; everything else opens as text.
    bool program = strncmp(p, "/system/bin/", 12) == 0;
    int pid = program ? ui::launch(p) : ui::launch("/system/bin/edit", p);
    if (pid < 0) report("Cannot open the item", pid);
    else {
        char msg[200];
        snprintf(msg, sizeof msg, program ? "Started %s" : "Opened %s in Text Editor", e->name);
        set_status(msg);
    }
}

// A name not used in the current folder: "base", "base 2", "base 3"...
void unique_name(const char* base, char* out, size_t n) {
    char p[FIH_PATH_MAX];
    strlcpy(out, base, n);
    const char* dot = strrchr(base, '.');
    int stem = dot && dot != base ? (int)(dot - base) : (int)strlen(base);
    for (int i = 2; i < 1000; i++) {
        fs::join(cwd, out, p, sizeof p);
        if (!fs::exists(p)) return;
        snprintf(out, n, "%.*s %d%s", stem, base, i, base + stem);
    }
}

void paste() {
    if (!clip_path[0]) return set_status("Nothing to paste");
    if (!fs::exists(clip_path)) {
        clip_path[0] = 0;
        return report("The copied item no longer exists", -FIH_ENOENT);
    }
    char name[FIH_NAME_MAX + 3], dst[FIH_PATH_MAX];
    if (clip_cut) {
        strlcpy(name, fs::basename(clip_path), sizeof name);
        fs::join(cwd, name, dst, sizeof dst);
        if (!strcmp(dst, clip_path)) return set_status("The item is already here");
        if (fs::exists(dst)) unique_name(name, name, sizeof name), fs::join(cwd, name, dst, sizeof dst);
        int e = fih_rename(clip_path, dst, 0);
        if (e == -FIH_EXDEV) { // different filesystems: copy, then delete
            e = fs::copy(clip_path, dst);
            if (!e) e = fs::remove_tree(clip_path);
        }
        if (e) return report("Cannot move the item", e);
        clip_path[0] = 0;
        set_status("Moved");
    } else {
        unique_name(fs::basename(clip_path), name, sizeof name);
        fs::join(cwd, name, dst, sizeof dst);
        set_status("Copying...");
        int e = fs::copy(clip_path, dst);
        if (e) return report("Cannot copy the item", e);
        set_status("Copied");
    }
    refresh(name);
}

void tool(int t) {
    Entry* sel = selected();
    char p[FIH_PATH_MAX];
    switch (t) {
    case T_BACK:
        if (nhistory) navigate(history[--nhistory], false);
        break;
    case T_UP: {
        char parent[FIH_PATH_MAX];
        fs::dirname(cwd, parent, sizeof parent);
        if (strcmp(parent, cwd)) navigate(parent);
        break;
    }
    case T_NEW_FOLDER: {
        char name[64];
        unique_name("New Folder", name, sizeof name);
        dialog.prompt(DLG_NEW_FOLDER, "New Folder", "Folder name:", name, "Create");
        dialog.input.select_all();
        break;
    }
    case T_NEW_FILE: {
        char name[64];
        unique_name("New Document.txt", name, sizeof name);
        dialog.prompt(DLG_NEW_FILE, "New Document", "File name:", name, "Create");
        break;
    }
    case T_RENAME:
        if (sel) dialog.prompt(DLG_RENAME, "Rename", "New name:", sel->name, "Rename");
        break;
    case T_DELETE:
        if (sel) {
            char msg[200];
            snprintf(msg, sizeof msg, sel->type == FIH_FT_DIR ? "Delete folder \"%s\" and everything in it?"
                                                               : "Delete \"%s\"? This cannot be undone.", sel->name);
            dialog.confirm(DLG_DELETE, "Delete", msg, "Delete", "Cancel");
        }
        break;
    case T_COPY: case T_CUT:
        if (sel) {
            selected_path(clip_path, sizeof clip_path);
            clip_cut = t == T_CUT;
            snprintf(p, sizeof p, "%s \"%s\": open a folder and choose Paste", clip_cut ? "Cut" : "Copied", sel->name);
            set_status(p);
        }
        break;
    case T_PASTE: paste(); break;
    }
}

void dialog_done() {
    int tag = dialog.tag, r = dialog.result;
    char text[FIH_PATH_MAX];
    strlcpy(text, dialog.input.text, sizeof text);
    dialog.close();
    if (r != 0) return;
    char p[FIH_PATH_MAX];
    int e = 0;
    switch (tag) {
    case DLG_NEW_FOLDER:
        if (!(e = fs::join(cwd, text, p, sizeof p))) e = fih_mkdir(p);
        if (e) report("Cannot create the folder", e);
        else refresh(text);
        break;
    case DLG_NEW_FILE:
        if (!(e = fs::join(cwd, text, p, sizeof p))) {
            int fd = fih_open(p, FIH_O_WRITE | FIH_O_CREATE | FIH_O_EXCL);
            e = fd < 0 ? fd : fih_close(fd);
        }
        if (e) report("Cannot create the document", e);
        else refresh(text);
        break;
    case DLG_RENAME: {
        char from[FIH_PATH_MAX];
        selected_path(from, sizeof from);
        if (strchr(text, '/')) e = -FIH_EINVAL;
        else if (!(e = fs::join(cwd, text, p, sizeof p))) e = fih_rename(from, p, 0);
        if (e) report("Cannot rename the item", e);
        else refresh(text);
        break;
    }
    case DLG_DELETE:
        selected_path(p, sizeof p);
        e = fs::remove_tree(p);
        if (e) report("Cannot delete the item", e);
        else set_status("Deleted");
        refresh();
        break;
    }
}

void draw() {
    gfx::Canvas& c = win.canvas;
    c.clip = {0, 0, win.w, win.h};
    c.fill({0, 0, win.w, theme::toolbar_h}, theme::panel);
    c.hline(0, theme::toolbar_h - 1, win.w, theme::border);
    const ui::Icon icons[4] = {ui::Icon::Back, ui::Icon::Up, ui::Icon::NewFolder, ui::Icon::NewFile};
    for (int i = 0; i < T_COUNT; i++) {
        Rect r = tool_rect(i);
        bool disabled = (i == T_BACK && !nhistory) || (i >= T_RENAME && i <= T_CUT && !selected()) ||
                        (i == T_PASTE && !clip_path[0]) ||
                        (readonly_dir && (i == T_NEW_FOLDER || i == T_NEW_FILE || i == T_RENAME || i == T_DELETE ||
                                          i == T_CUT || i == T_PASTE));
        ui::draw_button(c, r, TOOL_LABEL[i], ui::Normal, pressed == i, disabled);
        if (i < 4) ui::draw_icon(c, icons[i], r.x + 8, r.y + 6, 16);
        if (disabled && i < 4) c.blend(r.inset(2), theme::field, 150);
    }
    location.draw(c, location_rect(), location_focused);

    Rect h = header_rect();
    c.fill(h, theme::bg);
    c.hline(0, h.y + h.h - 1, win.w, theme::border);
    int col_size = win.w - 260, col_date = win.w - 160;
    c.text(40, h.y + 4, "Name", theme::text_dim, font_sans);
    c.text(col_size, h.y + 4, "Size", theme::text_dim, font_sans);
    c.text(col_date, h.y + 4, "Modified", theme::text_dim, font_sans);

    list.area = list_rect();
    list.clamp();
    c.fill(list.area, theme::field);
    c.clip = list.area;
    for (int i = list.top; i < nentries && i < list.top + list.visible() + 1; i++) {
        Rect r = list.row_rect(i);
        Entry& e = entries[i];
        bool sel = i == list.selected;
        if (sel) c.fill(r, win.focused && !location_focused ? theme::select_strong : theme::select);
        uint32_t fg = sel && win.focused && !location_focused ? 0xFFFFFF : theme::text;
        ui::Icon icon = e.type == FIH_FT_DIR ? ui::Icon::Folder
                      : strncmp(cwd, "/system/bin", 11) == 0 ? ui::Icon::Program : ui::Icon::Text;
        ui::draw_icon(c, icon, 14, r.y + 4, 16);
        c.text(40, r.y + 4, e.name, fg, font_sans, col_size - 52);
        char buf[40];
        if (e.type == FIH_FT_DIR) strlcpy(buf, "Folder", sizeof buf);
        else ui::format_size(buf, sizeof buf, e.size);
        c.text(col_size, r.y + 4, buf, sel ? fg : theme::text_dim, font_sans);
        if (e.mtime) {
            ui::format_time(buf, sizeof buf, e.mtime);
            c.text(col_date, r.y + 4, buf, sel ? fg : theme::text_dim, font_sans);
        }
    }
    if (!nentries) c.text(40, list.area.y + 12, "This folder is empty", theme::text_dim, font_sans);
    c.clip = {0, 0, win.w, win.h};
    list.draw_scrollbar(c);

    Rect sb{0, win.h - theme::status_h, win.w, theme::status_h};
    c.fill(sb, theme::panel);
    c.hline(0, sb.y, win.w, theme::border);
    char info[200];
    fih_statfs_t sf;
    char freebuf[24] = "";
    if (fih_statfs(cwd, &sf) == 0 && sf.block_size && !(sf.flags & FIH_ST_READONLY))
        ui::format_size(freebuf, sizeof freebuf, sf.free_blocks * sf.block_size);
    snprintf(info, sizeof info, "%d items%s%s%s", nentries, readonly_dir ? "  (read-only)" : "",
             freebuf[0] ? "  -  free: " : "", freebuf);
    int iw = gfx::text_width(font_sans, info);
    c.text(sb.w - iw - 12, sb.y + 4, info, theme::text_dim, font_sans);
    c.text(10, sb.y + 4, status, theme::text_dim, font_sans, sb.w - iw - 40);
    dialog.draw(c);
    win.present();
}

void on_key(const Event& e) {
    if (location_focused) {
        if (e.key == FIH_KEY_ENTER) { location_focused = false; navigate(location.text); return; }
        if (e.key == FIH_KEY_ESC || e.key == FIH_KEY_TAB) { location_focused = false; location.set(cwd); return; }
        location.key(e);
        return;
    }
    if (e.ctrl()) {
        switch (e.ch | 32) {
        case 'n': tool(e.shift() ? T_NEW_FOLDER : T_NEW_FILE); return;
        case 'c': tool(T_COPY); return;
        case 'x': tool(T_CUT); return;
        case 'v': tool(T_PASTE); return;
        case 'l': location_focused = true; location.select_all(); return;
        case 'w': case 'q': win.close(); fih_exit(0);
        }
    }
    switch (e.key) {
    case FIH_KEY_ENTER: open_selected(); return;
    case FIH_KEY_BACKSPACE: tool(e.alt() ? T_BACK : T_UP); return;
    case FIH_KEY_DELETE: tool(T_DELETE); return;
    case FIH_KEY_F2: tool(T_RENAME); return;
    case FIH_KEY_F5: refresh(); set_status("Refreshed"); return;
    case FIH_KEY_LEFT: if (e.alt()) tool(T_BACK); return;
    case FIH_KEY_TAB: location_focused = true; location.select_all(); return;
    }
    if (list.key(e)) return;
    // Type-ahead: jump to the next entry starting with the typed letter.
    if (e.ch > 32 && e.ch < 127 && !e.ctrl() && !e.alt() && nentries) {
        for (int k = 1; k <= nentries; k++) {
            int i = (list.selected + k) % nentries;
            if ((entries[i].name[0] | 32) == ((char)e.ch | 32)) {
                list.selected = i;
                list.ensure_visible();
                return;
            }
        }
    }
}

void on_mouse(const Event& e) {
    if (list.mouse(e)) return;
    if (e.type == Event::MouseDown) {
        if (location_rect().contains(e.x, e.y)) {
            location_focused = true;
            location.click(e, location_rect());
            return;
        }
        location_focused = false;
        location.set(cwd);
        for (int i = 0; i < T_COUNT; i++) if (tool_rect(i).contains(e.x, e.y)) { pressed = i; return; }
        int i = list.hit(e.x, e.y);
        if (i >= 0) {
            list.selected = i;
            if (e.clicks == 2) open_selected();
        }
    } else if (e.type == Event::MouseMove && location_focused) {
        location.click(e, location_rect());
    } else if (e.type == Event::MouseUp) {
        if (pressed >= 0 && tool_rect(pressed).contains(e.x, e.y)) tool(pressed);
        pressed = -1;
    }
}
}

int main(int argc, char** argv) {
    if (!win.open("Files", 760, 480)) return 1;
    const char* start = argc > 1 ? argv[1] : (fs::is_dir("/home") ? "/home" : "/");
    navigate(start, false);
    draw();
    uint32_t last_refresh = fih_uptime();
    for (;;) {
        ui::Window* w;
        Event e;
        bool got = ui::next_event(&w, &e, 2000);
        if (!got) {
            // Pick up changes made by other programs (terminal, editor saves).
            if (!dialog.active() && fih_uptime() - last_refresh >= 2000) {
                refresh();
                last_refresh = fih_uptime();
                draw();
            }
            continue;
        }
        if (dialog.active()) {
            if (e.type == Event::Close) { win.close(); return 0; }
            if (dialog.handle(e)) dialog_done();
        } else {
            switch (e.type) {
            case Event::Key: on_key(e); break;
            case Event::MouseDown: case Event::MouseUp: case Event::MouseMove: case Event::Wheel: on_mouse(e); break;
            case Event::Close: win.close(); return 0;
            case Event::FocusIn: refresh(); break;
            default: break;
            }
        }
        draw();
    }
}
