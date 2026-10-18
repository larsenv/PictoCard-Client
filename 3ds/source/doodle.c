/* Doodle pad: draw with the stylus. The palette, brush sizes and tools (draw,
 * fill, erase, undo, clear) follow the doodle pad on pictocard.net and the Wii
 * app; the canvas has the same 400 x 232 size, so it fills the top screen. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "doodle.h"
#include "gfx.h"
#include "theme.h"

#define CANVAS_W 400
#define CANVAS_H 232
#define MAX_UNDO 6
#define FILL_TOLERANCE 130000 /* squared RGB distance, generous so fills swallow anti-aliased edges */

/* the canvas on the touch screen, at 3/4 size */
#define PAD_X 10
#define PAD_Y 30
#define PAD_SCALE 0.75f
#define PAD_W 300
#define PAD_H 174
#define BAR_Y 208

enum { TOOL_DRAW, TOOL_FILL, TOOL_ERASE, TOOL_COUNT };
enum { BTN_SIZE = TOOL_COUNT, BTN_UNDO, BTN_CLEAR, BTN_DONE, BTN_COUNT };

static const u32 COLORS[16] = {
    0x000000, 0x555555, 0xaaaaaa, 0xffffff, 0x800000, 0xff0000, 0xff9900, 0xffff00,
    0x800080, 0x004d00, 0x00ff00, 0x000080, 0x00ffff, 0xff69b4, 0xff00ff, 0x8b4513,
};
static const int SIZES[4] = { 2, 5, 10, 20 };
static const char *BTN_NAMES[BTN_COUNT] = { "Draw", "Fill", "Erase", "Size", "Undo", "Clear", "Done" };

static struct {
    int active;
    image_t *img;                /* the drawing, kept between visits */
    image_t *undo[MAX_UNDO];
    int undo_count;
    int color, size, tool;
    int drawing, last_x, last_y; /* stroke in progress, in canvas pixels */
    int dirty;
    texture_t tex;
    doodle_cb cb;
} dd;

static void blank(void) { memset(dd.img->px, 0xFF, (size_t)CANVAS_W * CANVAS_H * 4); }

static void push_undo(void)
{
    image_t *copy = image_new(CANVAS_W, CANVAS_H);
    if (!copy) return;
    memcpy(copy->px, dd.img->px, (size_t)CANVAS_W * CANVAS_H * 4);
    if (dd.undo_count == MAX_UNDO) {
        image_free(dd.undo[0]);
        memmove(dd.undo, dd.undo + 1, sizeof(dd.undo[0]) * (MAX_UNDO - 1));
        dd.undo_count--;
    }
    dd.undo[dd.undo_count++] = copy;
}

static void undo(void)
{
    if (!dd.undo_count) return;
    image_t *last = dd.undo[--dd.undo_count];
    memcpy(dd.img->px, last->px, (size_t)CANVAS_W * CANVAS_H * 4);
    image_free(last);
    dd.dirty = 1;
}

static void put(int x, int y, u32 c)
{
    if (x < 0 || y < 0 || x >= CANVAS_W || y >= CANVAS_H) return;
    u8 *p = dd.img->px + ((size_t)y * CANVAS_W + x) * 4;
    p[0] = c >> 16;
    p[1] = c >> 8;
    p[2] = c;
    p[3] = 0xFF;
}

static void dot(int cx, int cy, int r, u32 c)
{
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r) put(cx + x, cy + y, c);
}

static void line(int x0, int y0, int x1, int y1, int r, u32 c)
{
    int dx = abs(x1 - x0), dy = abs(y1 - y0), steps = dx > dy ? dx : dy;
    if (!steps) {
        dot(x0, y0, r, c);
        return;
    }
    for (int i = 0; i <= steps; i++) dot(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps, r, c);
}

static int close_to(const u8 *p, const u8 *ref)
{
    int dr = p[0] - ref[0], dg = p[1] - ref[1], db = p[2] - ref[2];
    return dr * dr + dg * dg + db * db <= FILL_TOLERANCE;
}

static void fill(int sx, int sy, u32 c)
{
    if (sx < 0 || sy < 0 || sx >= CANVAS_W || sy >= CANVAS_H) return;
    u8 ref[4];
    memcpy(ref, dd.img->px + ((size_t)sy * CANVAS_W + sx) * 4, 4);
    u8 *seen = calloc(CANVAS_W * CANVAS_H, 1);
    int *stack = malloc(sizeof(int) * CANVAS_W * CANVAS_H);
    if (!seen || !stack) {
        free(seen);
        free(stack);
        return;
    }
    int top = 0;
    stack[top++] = sy * CANVAS_W + sx;
    seen[sy * CANVAS_W + sx] = 1;
    while (top) {
        int i = stack[--top], x = i % CANVAS_W, y = i / CANVAS_W;
        put(x, y, c);
        static const int D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int k = 0; k < 4; k++) {
            int nx = x + D[k][0], ny = y + D[k][1];
            if (nx < 0 || ny < 0 || nx >= CANVAS_W || ny >= CANVAS_H) continue;
            int j = ny * CANVAS_W + nx;
            if (seen[j] || !close_to(dd.img->px + (size_t)j * 4, ref)) continue;
            seen[j] = 1;
            stack[top++] = j;
        }
    }
    free(seen);
    free(stack);
}

void doodle_open(doodle_cb cb)
{
    if (!dd.img) {
        dd.img = image_new(CANVAS_W, CANVAS_H);
        if (!dd.img) return;
        blank();
        dd.size = 1;
    }
    dd.cb = cb;
    dd.active = 1;
    dd.drawing = 0;
    dd.dirty = 1;
}

int doodle_active(void) { return dd.active; }

static void close_pad(int keep)
{
    dd.active = 0;
    dd.drawing = 0;
    if (keep && dd.cb) dd.cb(dd.img);
    tex_free(&dd.tex);
}

static void button(int b)
{
    switch (b) {
    case TOOL_DRAW: case TOOL_FILL: case TOOL_ERASE: dd.tool = b; break;
    case BTN_SIZE: dd.size = (dd.size + 1) % 4; break;
    case BTN_UNDO: undo(); break;
    case BTN_CLEAR:
        push_undo();
        blank();
        dd.dirty = 1;
        break;
    case BTN_DONE: close_pad(1); break;
    }
}

void doodle_update(const input_t *in)
{
    if (!dd.active) return;
    if (in->down & KEY_B) {
        close_pad(0);
        return;
    }
    if (in->down & KEY_START) {
        close_pad(1);
        return;
    }
    if (in->down & (KEY_L | KEY_ZL)) undo();
    if (in->repeat & KEY_DLEFT) dd.color = (dd.color + 15) % 16;
    if (in->repeat & KEY_DRIGHT) dd.color = (dd.color + 1) % 16;
    if (in->down & KEY_DUP) dd.size = (dd.size + 1) % 4;
    if (in->down & KEY_DDOWN) dd.size = (dd.size + 3) % 4;
    if (in->down & KEY_Y) dd.tool = (dd.tool + 1) % TOOL_COUNT;

    int cx = (int)((in->x - PAD_X) / PAD_SCALE), cy = (int)((in->y - PAD_Y) / PAD_SCALE);
    int on_canvas = in->x >= PAD_X && in->x < PAD_X + PAD_W && in->y >= PAD_Y && in->y < PAD_Y + PAD_H;
    if (in->pressed) {
        if (in->y < 26) {
            int c = in->x / 20;
            if (c >= 0 && c < 16) dd.color = c;
        } else if (in->y >= BAR_Y) {
            int b = in->x * BTN_COUNT / BOT_W;
            if (b >= 0 && b < BTN_COUNT) button(b);
            return;
        } else if (on_canvas) {
            push_undo();
            if (dd.tool == TOOL_FILL) {
                fill(cx, cy, COLORS[dd.color]);
            } else {
                dd.drawing = 1;
                dd.last_x = cx;
                dd.last_y = cy;
                line(cx, cy, cx, cy, SIZES[dd.size] / 2, dd.tool == TOOL_ERASE ? 0xFFFFFF : COLORS[dd.color]);
            }
            dd.dirty = 1;
        }
    } else if (in->touching && dd.drawing) {
        if (cx < 0) cx = 0;
        if (cy < 0) cy = 0;
        if (cx >= CANVAS_W) cx = CANVAS_W - 1;
        if (cy >= CANVAS_H) cy = CANVAS_H - 1;
        if (cx != dd.last_x || cy != dd.last_y) {
            line(dd.last_x, dd.last_y, cx, cy, SIZES[dd.size] / 2, dd.tool == TOOL_ERASE ? 0xFFFFFF : COLORS[dd.color]);
            dd.last_x = cx;
            dd.last_y = cy;
            dd.dirty = 1;
        }
    }
    if (!in->touching) dd.drawing = 0;
}

static void upload(void)
{
    if (dd.dirty && tex_set(&dd.tex, dd.img)) dd.dirty = 0;
}

void doodle_draw_top(void)
{
    upload();
    rect(0, 0, TOP_W, SCR_H, rgb(T->bg));
    tex_draw(&dd.tex, 0, 4, 1, 1);
    frame_rect(0, 3, TOP_W, CANVAS_H + 2, 1, rgb(T->line));
}

void doodle_draw_bottom(void)
{
    upload();
    rect(0, 0, BOT_W, SCR_H, rgb(T->panel));
    for (int i = 0; i < 16; i++) {
        rect(i * 20 + 1, 2, 18, 22, rgb(COLORS[i]));
        if (i == dd.color) frame_rect(i * 20, 1, 20, 24, 2, rgb(T->accent));
    }
    rect(PAD_X - 1, PAD_Y - 1, PAD_W + 2, PAD_H + 2, rgb(T->line));
    tex_draw(&dd.tex, PAD_X, PAD_Y, PAD_SCALE, PAD_SCALE);
    float bw = BOT_W / (float)BTN_COUNT;
    for (int b = 0; b < BTN_COUNT; b++) {
        int on = b == dd.tool || b == BTN_DONE;
        rect(b * bw + 1, BAR_Y, bw - 2, SCR_H - BAR_Y - 2, rgb(on ? T->accent : T->elev));
        char label[16];
        if (b == BTN_SIZE) snprintf(label, sizeof label, "%dpx", SIZES[dd.size]);
        else snprintf(label, sizeof label, "%s", BTN_NAMES[b]);
        text(b * bw + bw / 2, BAR_Y + 8, 12, rgb(on ? T->on_accent : T->text), C2D_AlignCenter, 0, label);
    }
}
