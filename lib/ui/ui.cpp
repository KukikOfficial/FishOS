#include "fishos/ui.h"
#include <fihcore/syscall.h>
#include <stdlib.h>
#include <string.h>

namespace ui {
namespace {
constexpr int MAX_WINDOWS = 16;
int server;
Window* windows[MAX_WINDOWS];
uint32_t next_token = 1;

// Messages that arrived while waiting for a specific reply.
constexpr int PENDING_MAX = 64;
fih_msg_t pending[PENDING_MAX];
int pending_head, pending_count;

void push_pending(const fih_msg_t& m) {
    if (pending_count == PENDING_MAX) { // keep the newest messages
        pending_head = (pending_head + 1) % PENDING_MAX;
        pending_count--;
    }
    pending[(pending_head + pending_count++) % PENDING_MAX] = m;
}

int receive(fih_msg_t* m, int timeout) {
    if (pending_count) {
        *m = pending[pending_head];
        pending_head = (pending_head + 1) % PENDING_MAX;
        pending_count--;
        return 1;
    }
    return fih_msg_recv(m, timeout);
}

bool wait_reply(uint32_t type, fih_msg_t* out, int timeout_ms) {
    uint32_t deadline = fih_uptime() + (uint32_t)timeout_ms;
    for (;;) {
        int left = (int)(deadline - fih_uptime());
        if (left <= 0) return false;
        fih_msg_t m;
        if (fih_msg_recv(&m, left) != 1) return false;
        if (m.type == type && m.from == (uint32_t)server) {
            *out = m;
            return true;
        }
        push_pending(m);
    }
}

bool send(fih_msg_t& m) { return server > 0 && fih_msg_send(server, &m) == 0; }

void pack_title(fih_msg_t& m, int first, int max, const char* title) {
    char* dst = reinterpret_cast<char*>(&m.a[first]);
    memset(dst, 0, (size_t)max);
    strlcpy(dst, title, (size_t)max);
}

Window* find(uint32_t id) {
    for (Window* w : windows)
        if (w && (uint32_t)w->id == id) return w;
    return nullptr;
}
}

bool connect() {
    if (server > 0) return true;
    for (int tries = 0; tries < 50 && server <= 0; tries++) {
        server = fih_name_lookup(WS_SERVICE);
        if (server <= 0) fih_sleep(20);
    }
    return server > 0;
}

bool Window::alloc_buffer(int width, int height) {
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    int id_new = fih_shm_create((uint32_t)(width * height * 4));
    if (id_new < 0) return false;
    auto* px = static_cast<uint32_t*>(fih_shm_map(id_new));
    if (!px || fih_shm_grant(id_new, server) < 0) {
        fih_shm_destroy(id_new);
        return false;
    }
    if (shm) { // the server keeps its own mapping until it switches buffers
        fih_shm_unmap(shm);
        fih_shm_destroy(shm);
    }
    shm = id_new;
    w = width;
    h = height;
    canvas.reset(px, width, height, width);
    canvas.fill({0, 0, width, height}, theme::bg);
    return true;
}

bool Window::open(const char* title, int width, int height, uint32_t flags) {
    if (!connect()) return false;
    int slot = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) if (!windows[i]) { slot = i; break; }
    if (slot < 0 || !alloc_buffer(width, height)) return false;
    fih_msg_t m{};
    m.type = WS_CREATE;
    m.a[0] = (uint32_t)shm;
    m.a[1] = (uint32_t)width;
    m.a[2] = (uint32_t)height;
    m.a[3] = flags;
    uint32_t token = next_token++;
    m.a[4] = token;
    pack_title(m, 5, WS_TITLE_CREATE, title);
    fih_msg_t reply;
    if (!send(m)) return false;
    for (;;) {
        if (!wait_reply(WS_CREATED, &reply, 3000)) return false;
        if (reply.a[0] == token) break;
    }
    if ((int)reply.a[1] <= 0) return false;
    id = (int)reply.a[1];
    windows[slot] = this;
    return true;
}

void Window::close() {
    if (!id) return;
    fih_msg_t m{};
    m.type = WS_DESTROY;
    m.a[0] = (uint32_t)id;
    send(m);
    for (Window*& w : windows) if (w == this) w = nullptr;
    fih_shm_unmap(shm);
    fih_shm_destroy(shm);
    shm = 0;
    id = 0;
}

void Window::present() { present({0, 0, w, h}); }

void Window::present(Rect r) {
    r = r.intersect({0, 0, w, h});
    if (r.empty() || !id) return;
    fih_msg_t m{};
    m.type = WS_DAMAGE;
    m.a[0] = (uint32_t)id;
    m.a[1] = (uint32_t)r.x;
    m.a[2] = (uint32_t)r.y;
    m.a[3] = (uint32_t)r.w;
    m.a[4] = (uint32_t)r.h;
    send(m);
}

void Window::set_title(const char* title) {
    fih_msg_t m{};
    m.type = WS_SET_TITLE;
    m.a[0] = (uint32_t)id;
    pack_title(m, 1, WS_TITLE_MAX, title);
    send(m);
}

void Window::activate() {
    fih_msg_t m{};
    m.type = WS_ACTIVATE;
    m.a[0] = (uint32_t)id;
    send(m);
}

bool next_event(Window** win, Event* ev, int timeout_ms) {
    uint32_t deadline = fih_uptime() + (uint32_t)(timeout_ms < 0 ? 0 : timeout_ms);
    for (;;) {
        int left = timeout_ms < 0 ? -1 : (int)(deadline - fih_uptime());
        if (timeout_ms >= 0 && left < 0) left = 0;
        fih_msg_t m;
        if (receive(&m, left) != 1) return false;
        if (m.type == FIH_MSG_CHILD_EXIT && m.from == 0) {
            fih_wait((int)m.a[0], nullptr, FIH_WNOHANG);
            continue;
        }
        if (m.from != (uint32_t)server || m.type < WS_CREATED) continue;
        Window* w = find(m.a[0]);
        if (!w) continue;
        *ev = Event{};
        switch (m.type) {
        case WS_EV_KEY:
            ev->type = m.a[2] ? Event::Key : Event::KeyUp;
            ev->key = m.a[1];
            ev->ch = m.a[3];
            ev->mods = m.a[4];
            break;
        case WS_EV_MOUSE:
            ev->type = m.a[1] == WS_MOUSE_DOWN ? Event::MouseDown
                     : m.a[1] == WS_MOUSE_UP ? Event::MouseUp
                     : m.a[1] == WS_MOUSE_WHEEL ? Event::Wheel : Event::MouseMove;
            ev->x = (int)m.a[2];
            ev->y = (int)m.a[3];
            ev->buttons = m.a[4];
            ev->wheel = (int)m.a[5];
            ev->clicks = (int)m.a[6];
            break;
        case WS_EV_FOCUS:
            w->focused = m.a[1] != 0;
            ev->type = w->focused ? Event::FocusIn : Event::FocusOut;
            break;
        case WS_EV_CLOSE:
            ev->type = Event::Close;
            break;
        case WS_EV_RESIZE: {
            int nw = (int)m.a[1], nh = (int)m.a[2];
            if (nw == w->w && nh == w->h) continue;
            if (!w->alloc_buffer(nw, nh)) continue; // keep the old size
            fih_msg_t b{};
            b.type = WS_BUFFER;
            b.a[0] = (uint32_t)w->id;
            b.a[1] = (uint32_t)w->shm;
            b.a[2] = (uint32_t)nw;
            b.a[3] = (uint32_t)nh;
            send(b);
            ev->type = Event::Resize;
            ev->w = nw;
            ev->h = nh;
            break;
        }
        default:
            continue;
        }
        *win = w;
        return true;
    }
}

bool clipboard_set(const char* text, int len) {
    if (!connect() || len < 0 || len > WS_CLIP_MAX) return false;
    int id = fih_shm_create((uint32_t)(len > 0 ? len : 1));
    if (id < 0) return false;
    char* p = static_cast<char*>(fih_shm_map(id));
    bool ok = false;
    if (p && fih_shm_grant(id, server) == 0) {
        memcpy(p, text, (size_t)len);
        fih_msg_t m{};
        m.type = WS_CLIP_SET;
        m.a[0] = (uint32_t)id;
        m.a[1] = (uint32_t)len;
        fih_msg_t r;
        ok = send(m) && wait_reply(WS_CLIP_ACK, &r, 2000) && r.a[0] == 0;
    }
    fih_shm_unmap(id);
    fih_shm_destroy(id);
    return ok;
}

char* clipboard_get(int* len) {
    *len = 0;
    if (!connect()) return nullptr;
    fih_msg_t m{};
    m.type = WS_CLIP_GET;
    fih_msg_t r;
    if (!send(m) || !wait_reply(WS_CLIP_DATA, &r, 2000) || !r.a[0]) return nullptr;
    int id = (int)r.a[0];
    int n = (int)r.a[1];
    char* out = nullptr;
    const char* p = static_cast<const char*>(fih_shm_map(id));
    if (p && n >= 0 && n <= WS_CLIP_MAX && (out = static_cast<char*>(malloc((size_t)n + 1)))) {
        memcpy(out, p, (size_t)n);
        out[n] = 0;
        *len = n;
    }
    fih_shm_unmap(id);
    fih_msg_t done{};
    done.type = WS_CLIP_DONE;
    done.a[0] = (uint32_t)id;
    send(done);
    return out;
}

int launch(const char* path, const char* arg) {
    const char* argv[3] = {path, arg, nullptr};
    return fih_spawn(path, argv, nullptr);
}

const char* error_string(int err) {
    switch (err < 0 ? -err : err) {
    case FIH_ENOENT: return "No such file or folder";
    case FIH_EEXIST: return "An item with that name already exists";
    case FIH_ENOTDIR: return "Not a folder";
    case FIH_EISDIR: return "Is a folder";
    case FIH_ENOTEMPTY: return "Folder is not empty";
    case FIH_ENOSPC: return "The disk is full";
    case FIH_EROFS: return "This location is read-only";
    case FIH_EIO: return "Disk I/O error";
    case FIH_ENAMETOOLONG: return "Name is too long";
    case FIH_EINVAL: return "Invalid name or operation";
    case FIH_EFBIG: return "File is too large";
    case FIH_ENOMEM: return "Out of memory";
    case FIH_EBUSY: return "Resource is busy";
    case FIH_EXDEV: return "Cannot move between disks";
    case FIH_ENOEXEC: return "Not an executable program";
    case FIH_ECORRUPT: return "Filesystem is damaged";
    case FIH_ENODEV: return "No storage device";
    case FIH_EPERM: return "Operation not permitted";
    default: return "Unexpected error";
    }
}

} // namespace ui
