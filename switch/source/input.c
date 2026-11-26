#include <stdlib.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <switch.h>
#include <input_switch.h>
#include "input.h"

#define REPEAT_DELAY 22 /* frames before a held direction repeats */
#define REPEAT_RATE  5

static int canvas_w = 852, canvas_h = 480;
static int last_px = -1000, last_py = -1000;
static u32 prev_dirs;
static int dir_frames;
static int quit_requested;

/* the touch screen (handheld mode) */
static u32 prev_held;
static int touch_down, touch_release, touch_x, touch_y;
static int touch_start_x, touch_start_y, touch_dragged;
int input_drawing; /* a touch holds A (the doodle pad) instead of tapping */
#define DRAG_SLOP 10 /* canvas pixels a finger may wander and still tap */

/* ── Controller mapping ──────────────────────────────────────────────────── */

static u32 map_pad(u64 b)
{
    u32 out = 0;
    if (b & (HidNpadButton_R | HidNpadButton_ZR | HidNpadButton_Plus)) out |= BTN_PLUS;
    if (b & (HidNpadButton_L | HidNpadButton_ZL | HidNpadButton_Minus)) out |= BTN_MINUS;
    if (b & HidNpadButton_A) out |= BTN_A;
    if (b & HidNpadButton_B) out |= BTN_B;
    if (b & HidNpadButton_X) out |= BTN_2;
    if (b & HidNpadButton_Y) out |= BTN_1;
    if (b & HidNpadButton_StickR) out |= BTN_HOME; /* the Home button belongs to the system */
    if (b & (HidNpadButton_Up | HidNpadButton_StickLUp)) out |= BTN_UP;
    if (b & (HidNpadButton_Down | HidNpadButton_StickLDown)) out |= BTN_DOWN;
    if (b & (HidNpadButton_Left | HidNpadButton_StickLLeft)) out |= BTN_LEFT;
    if (b & (HidNpadButton_Right | HidNpadButton_StickLRight)) out |= BTN_RIGHT;
    return out;
}

/* ── USB keyboard (through libswitchinput) ───────────────────────────────── */

int input_key(key_event *ev)
{
    input_key_event k;
    if (!INPUT_KeyPop(&k)) return 0;
    ev->code = k.code;
    ev->shift = (k.mods & INPUT_MOD_SHIFT) != 0;
    ev->ctrl = (k.mods & INPUT_MOD_CTRL) != 0;
    ev->ch = ev->ctrl ? 0 : k.ch;
    return 1;
}

int input_keyboard_connected(void) { return INPUT_KeyboardConnected(); }

#ifdef AUTOTEST
void autotest_push_key(u8 code, char ch)
{
    (void)code; (void)ch;
}
void autotest_apply(input_t *in);
#endif

int input_quit_requested(void) { return quit_requested; }

/* ── Setup and polling ───────────────────────────────────────────────────── */

void input_init(void)
{
    INPUT_Init();
    INPUT_SetScreenSize(canvas_w, canvas_h);
}

void input_set_canvas(int width, int height)
{
    canvas_w = width;
    canvas_h = height;
    INPUT_SetScreenSize(width, height);
}

void input_touch_draws(int on) { input_drawing = on; }

void input_shutdown(void) { INPUT_Shutdown(); }

void input_poll(input_t *in)
{
    u32 down = 0, held = 0;

    SDL_Event e;
    while (SDL_PollEvent(&e))
        if (e.type == SDL_QUIT) quit_requested = 1;
    if (!appletMainLoop()) quit_requested = 1; /* the system asked the app to close */

    in->pointer = 0;
    in->touch = in->touch_held = in->touch_began = in->touch_drag = 0;

    /* Every controller at once: handheld Joy-Cons, a Pro Controller, a pair of detached Joy-Cons */
    INPUT_Update();
    for (int i = 0, n = INPUT_DeviceCount(); i < n; i++) {
        input_device d;
        if (INPUT_DeviceGet(i, &d)) held |= map_pad(d.raw); /* the device's own bits: + / R and - / L are folded otherwise */
    }
    down |= held & ~prev_held;
    prev_held = held;

    /* The touch screen: a tap (let go without dragging) is A, a drag scrolls */
    int tx, ty;
    int touched = INPUT_Touch(&tx, &ty);
    int was_down = touch_down && !touch_release;
    if (touch_release) touch_down = touch_release = 0;
    if (touched) {
        touch_x = tx;
        touch_y = ty;
        touch_down = 1;
        touch_release = 0;
    } else if (touch_down) {
        touch_release = 1; /* let go: this frame still reports where */
    }
    if (touch_down) {
        int began = !was_down;
        if (began) {
            touch_start_x = touch_x;
            touch_start_y = touch_y;
            touch_dragged = 0;
        }
        if (abs(touch_x - touch_start_x) > DRAG_SLOP || abs(touch_y - touch_start_y) > DRAG_SLOP) touch_dragged = 1;
        in->pointer = 1;
        in->touch = 1;
        in->touch_held = !touch_release;
        in->touch_began = began;
        in->touch_drag = touch_dragged;
        in->px = touch_x;
        in->py = touch_y;
        if (input_drawing) {
            if (began) down |= BTN_A;
            if (!touch_release) held |= BTN_A;
        } else if (touch_release && !touch_dragged) {
            down |= BTN_A; /* a tap */
        }
    }

    /* Directions: a new press fires at once, then repeats while held. */
    u32 dirs = held & BTN_DIRS;
    u32 fresh = (dirs & ~prev_dirs) | (down & BTN_DIRS);
    if (dirs && dirs == prev_dirs) dir_frames++;
    else dir_frames = 0;
    if (dir_frames >= REPEAT_DELAY && (dir_frames - REPEAT_DELAY) % REPEAT_RATE == 0) fresh |= dirs;
    prev_dirs = dirs;

    in->down = (down & ~BTN_DIRS) | fresh;
    in->held = held;

    if (in->pointer) {
        if (in->px < 0) in->px = 0;
        if (in->py < 0) in->py = 0;
        if (in->px >= canvas_w) in->px = canvas_w - 1;
        if (in->py >= canvas_h) in->py = canvas_h - 1;
        in->moved = in->touch || abs(in->px - last_px) > 3 || abs(in->py - last_py) > 3; /* ignore IR jitter */
        if (in->moved) {
            last_px = in->px;
            last_py = in->py;
        }
    } else {
        in->moved = 0;
    }
#ifdef AUTOTEST
    autotest_apply(in);
#endif
}
