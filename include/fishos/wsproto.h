// FishOS window system protocol, carried in Fihcore IPC messages.
//
// Clients find the display server with fih_name_lookup(WS_SERVICE). A window's
// pixels live in a shared memory object created by the client and granted to
// the server; the client draws into it and sends WS_DAMAGE for changed areas.
#pragma once
#include <fihcore/abi.h>

#define WS_SERVICE "fishos.display"
#define WS_TITLE_CREATE 36 // bytes of title carried by WS_CREATE (a5..a13)
#define WS_TITLE_MAX 52    // bytes carried by WS_SET_TITLE (a1..a13)

enum {
    WS_BASE = FIH_MSG_USER + 0x100,

    // client -> server
    WS_CREATE = WS_BASE,  // a0 shm, a1 w, a2 h, a3 flags, a4 token, a5.. title
    WS_DESTROY,           // a0 win
    WS_DAMAGE,            // a0 win, a1 x, a2 y, a3 w, a4 h
    WS_SET_TITLE,         // a0 win, a1.. title
    WS_BUFFER,            // a0 win, a1 shm, a2 w, a3 h: new buffer after WS_EV_RESIZE
    WS_ACTIVATE,          // a0 win: raise, restore and focus
    WS_CLIP_SET,          // a0 shm (granted to server), a1 len -> WS_CLIP_ACK
    WS_CLIP_GET,          // -> WS_CLIP_DATA
    WS_CLIP_DONE,         // a0 shm from WS_CLIP_DATA, may be released

    // server -> client
    WS_CREATED = WS_BASE + 0x40, // a0 token, a1 win id (or -error)
    WS_EV_KEY,            // a0 win, a1 keycode, a2 pressed, a3 char, a4 modifiers
    WS_EV_MOUSE,          // a0 win, a1 kind, a2 x, a3 y, a4 buttons, a5 wheel, a6 clicks
    WS_EV_FOCUS,          // a0 win, a1 focused
    WS_EV_CLOSE,          // a0 win: the user asked to close the window
    WS_EV_RESIZE,         // a0 win, a1 w, a2 h: client should send WS_BUFFER
    WS_CLIP_ACK,          // a0 0 or -error
    WS_CLIP_DATA,         // a0 shm (0 = empty clipboard), a1 len
};

enum { WS_MOUSE_MOVE = 0, WS_MOUSE_DOWN = 1, WS_MOUSE_UP = 2, WS_MOUSE_WHEEL = 3 };

#define WS_FLAG_RESIZABLE 1
#define WS_CLIP_MAX (256 * 1024)
