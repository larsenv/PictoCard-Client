/* Doodle pad: draw a picture on the GamePad, with the Wii Remote pointer or the
 * D-Pad. The palette, brush sizes and tools (draw, fill, erase, undo, clear)
 * follow the doodle pad on pictocard.net, which comes from Discross. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "app.h"
#include "input.h"
#include "font.h"
#include "gfx.h"
#include "ui.h"

#define CANVAS_W 400
#define CANVAS_H 232
#define MAX_UNDO 6
#define FILL_TOLERANCE 130000 /* squared RGB distance, generous so fills swallow anti-aliased edges */

enum { TOOL_DRAW, TOOL_FILL, TOOL_ERASE, TOOL_COUNT };
enum { BTN_TOOL_UNDO = TOOL_COUNT, BTN_TOOL_CLEAR, BTN_DONE, BTN_CANCEL };

static const u32 COLORS[16] = {
    0x000000, 0x555555, 0xaaaaaa, 0xffffff, 0x800000, 0xff0000, 0xff9900, 0xffff00,
    0x800080, 0x004d00, 0x00ff00, 0x000080, 0x00ffff, 0xff69b4, 0xff00ff, 0x8b4513,
};
static const int SIZES[4] = { 2, 5, 10, 20 };
static const char *TOOL_NAMES[] = { "Draw", "Fill", "Erase", "Undo", "Clear" };

static struct {
    int active;
    image_t *img;                /* the drawing, kept between visits */
    image_t *undo[MAX_UNDO];
    int undo_count;
    int color, size, tool;
    int drawing, last_x, last_y; /* stroke in progress, in canvas pixels */
    int key_stroke;              /* Shift+arrow stroke from the keyboard */
    int cx, cy;                  /* cursor in screen pixels */
    doodle_cb cb;
} dd;

static void geometry(int *px, int *py, int *pw, int *ph, int *cvx, int *cvy)
{
    *pw = 520;
    *ph = 396;
    *px = (SW - *pw) / 2;
    *py = 56;
    *cvx = *px + (*pw - CANVAS_W) / 2;
    *cvy = *py + 116;
}

static void blank(void)
{
    for (int i = 0; i < CANVAS_W * CANVAS_H; i++) dd.img->px[i] = 0xFFFFFFFF;
}

static void push_undo(void)
{
    image_t *copy = image_new(CANVAS_W, CANVAS_H);
    if (!copy) return;
    memcpy(copy->px, dd.img->px, sizeof(u32) * CANVAS_W * CANVAS_H);
    if (dd.undo_count == MAX_UNDO) {
        image_free(dd.undo[0]);
        memmove(&dd.undo[0], &dd.undo[1], sizeof(image_t *) * (MAX_UNDO - 1));
        dd.undo_count--;
    }
    dd.undo[dd.undo_count++] = copy;
}

static void undo(void)
{
    if (!dd.undo_count) return;
    image_t *prev = dd.undo[--dd.undo_count];
    memcpy(dd.img->px, prev->px, sizeof(u32) * CANVAS_W * CANVAS_H);
    image_free(prev);
}

static u32 ink(void) { return 0xFF000000 | (dd.tool == TOOL_ERASE ? 0xffffff : COLORS[dd.color]); }

static void disc(int cx, int cy, int diameter, u32 color)
{
    int r = diameter / 2;
    int r2 = r * r + (diameter > 2 ? r / 2 : 0);
    if (diameter <= 2) r = 1;
    for (int y = -r; y <= r; y++) {
        int py = cy + y;
        if (py < 0 || py >= CANVAS_H) continue;
        for (int x = -r; x <= r; x++) {
            int px = cx + x;
            if (px < 0 || px >= CANVAS_W) continue;
            if (diameter <= 2 || x * x + y * y <= r2) dd.img->px[py * CANVAS_W + px] = color;
        }
    }
}

static void line(int x0, int y0, int x1, int y1)
{
    int dx = x1 - x0, dy = y1 - y0;
    int steps = (int)sqrtf((float)(dx * dx + dy * dy)) / 2;
    if (steps < 1) steps = 1;
    u32 c = ink();
    for (int i = 0; i <= steps; i++) disc(x0 + dx * i / steps, y0 + dy * i / steps, SIZES[dd.size], c);
}

static void flood_fill(int sx, int sy)
{
    if (sx < 0 || sy < 0 || sx >= CANVAS_W || sy >= CANVAS_H) return;
    u32 *px = dd.img->px;
    u32 fill = 0xFF000000 | COLORS[dd.color];
    u32 target = px[sy * CANVAS_W + sx];
    if (target == fill) return;
    int tr = (target >> 16) & 255, tg = (target >> 8) & 255, tb = target & 255;
    u8 *seen = calloc(CANVAS_W * CANVAS_H, 1);
    int *stack = malloc(sizeof(int) * CANVAS_W * CANVAS_H);
    if (!seen || !stack) {
        free(seen);
        free(stack);
        return;
    }
    int top = 0;
    stack[top++] = sy * CANVAS_W + sx;
    while (top) {
        int pos = stack[--top];
        if (seen[pos]) continue;
        seen[pos] = 1;
        u32 p = px[pos];
        int dr = (int)((p >> 16) & 255) - tr, dg = (int)((p >> 8) & 255) - tg, db = (int)(p & 255) - tb;
        if (dr * dr + dg * dg + db * db > FILL_TOLERANCE) continue;
        px[pos] = fill;
        int x = pos % CANVAS_W;
        if (x > 0) stack[top++] = pos - 1;
        if (x < CANVAS_W - 1) stack[top++] = pos + 1;
        if (pos >= CANVAS_W) stack[top++] = pos - CANVAS_W;
        if (pos < CANVAS_W * (CANVAS_H - 1)) stack[top++] = pos + CANVAS_W;
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
    dd.active = 1;
    dd.cb = cb;
    dd.drawing = 0;
    dd.cx = SW / 2;
    dd.cy = SH / 2;
}

int doodle_active(void) { return dd.active; }

/* the buttons in the tool row, then Done and Cancel */
static int button_at(int x, int y, int px, int py, int pw, int ph)
{
    int tx = px + 20 + 4 * 36 + 16, ty = py + 80;
    if (y >= ty && y < ty + 28)
        for (int i = 0; i < 5; i++)
            if (x >= tx + i * 64 && x < tx + i * 64 + 60) return i;
    int by = py + ph - 40;
    if (y >= by && y < by + 28) {
        if (x >= px + pw - 20 - 100 && x < px + pw - 20) return BTN_DONE;
        if (x >= px + pw - 20 - 100 - 8 - 90 && x < px + pw - 20 - 108) return BTN_CANCEL;
    }
    return -1;
}

static void press_button(int b)
{
    if (b < TOOL_COUNT) {
        dd.tool = b;
    } else if (b == BTN_TOOL_UNDO) {
        undo();
    } else if (b == BTN_TOOL_CLEAR) {
        push_undo();
        blank();
    } else if (b == BTN_DONE) {
        dd.active = 0;
        if (dd.cb) dd.cb(dd.img);
    } else if (b == BTN_CANCEL) {
        dd.active = 0;
    }
}

void doodle_update(void *vin)
{
    input_t *in = vin;
    int px, py, pw, ph, cvx, cvy;
    geometry(&px, &py, &pw, &ph, &cvx, &cvy);

    /* USB keyboard: arrows move the cursor (Shift draws while moving, Ctrl moves a pixel),
     * Space stamps a dot or fills, C/X change colour, [ ] the brush, D/F/E pick the tool,
     * U or Ctrl+Z undoes, Enter uses the doodle and Esc cancels. */
    key_event ev;
    while (input_key(&ev)) {
        int dx = 0, dy = 0, step = ev.ctrl ? 1 : 8;
        if (ev.code == KEY_LEFT) dx = -step;
        else if (ev.code == KEY_RIGHT) dx = step;
        else if (ev.code == KEY_UP) dy = -step;
        else if (ev.code == KEY_DOWN) dy = step;
        if (dx || dy) {
            int ox = dd.cx - cvx, oy = dd.cy - cvy;
            dd.cx += dx;
            dd.cy += dy;
            if (dd.cx < 0) dd.cx = 0;
            if (dd.cy < 0) dd.cy = 0;
            if (dd.cx >= SW) dd.cx = SW - 1;
            if (dd.cy >= SH) dd.cy = SH - 1;
            int nx = dd.cx - cvx, ny = dd.cy - cvy;
            if (ev.shift && dd.tool != TOOL_FILL && ox >= 0 && oy >= 0 && ox < CANVAS_W && oy < CANVAS_H &&
                nx >= 0 && ny >= 0 && nx < CANVAS_W && ny < CANVAS_H) {
                if (!dd.key_stroke) {
                    push_undo();
                    dd.key_stroke = 1;
                }
                line(ox, oy, nx, ny);
            }
            if (!ev.shift) dd.key_stroke = 0;
            continue;
        }
        dd.key_stroke = 0;
        int ccx = dd.cx - cvx, ccy = dd.cy - cvy;
        char c = ev.ch;
        if (ev.code == KEY_ESC) in->down |= BTN_B;
        else if (ev.code == KEY_ENTER || ev.code == KEY_KP_ENTER) in->down |= BTN_PLUS;
        else if (ev.ctrl && ev.code == 0x1D) undo(); /* Ctrl+Z */
        else if (c == ' ' && ccx >= 0 && ccy >= 0 && ccx < CANVAS_W && ccy < CANVAS_H) {
            push_undo();
            if (dd.tool == TOOL_FILL) flood_fill(ccx, ccy);
            else disc(ccx, ccy, SIZES[dd.size], ink());
        } else if (c == 'c' || c == 'C') {
            dd.color = (dd.color + (c == 'c' ? 1 : 15)) % 16;
            if (dd.tool == TOOL_ERASE) dd.tool = TOOL_DRAW;
        } else if (c == 'x' || c == 'X') dd.color = (dd.color + 15) % 16;
        else if (c == ']') dd.size = dd.size < 3 ? dd.size + 1 : 3;
        else if (c == '[') dd.size = dd.size > 0 ? dd.size - 1 : 0;
        else if (c == 'd' || c == 'D') dd.tool = TOOL_DRAW;
        else if (c == 'f' || c == 'F') dd.tool = TOOL_FILL;
        else if (c == 'e' || c == 'E') dd.tool = TOOL_ERASE;
        else if (c == 'u' || c == 'U') undo();
    }

    if (in->pointer) {
        dd.cx = in->px;
        dd.cy = in->py;
    } else {
        int step = 3;
        if (in->held & BTN_LEFT) dd.cx -= step;
        if (in->held & BTN_RIGHT) dd.cx += step;
        if (in->held & BTN_UP) dd.cy -= step;
        if (in->held & BTN_DOWN) dd.cy += step;
        if (dd.cx < 0) dd.cx = 0;
        if (dd.cy < 0) dd.cy = 0;
        if (dd.cx >= SW) dd.cx = SW - 1;
        if (dd.cy >= SH) dd.cy = SH - 1;
    }
    int ccx = dd.cx - cvx, ccy = dd.cy - cvy;
    int on_canvas = ccx >= 0 && ccy >= 0 && ccx < CANVAS_W && ccy < CANVAS_H;

    if (in->down & BTN_B) {
        press_button(BTN_CANCEL);
        return;
    }
    if (in->down & BTN_PLUS) {
        press_button(BTN_DONE);
        return;
    }
    if (in->down & BTN_1) undo();
    if (in->down & BTN_2) dd.color = (dd.color + 1) % 16;
    if (in->down & BTN_MINUS) dd.size = (dd.size + 1) % 4;

    if (in->down & BTN_A) {
        if (on_canvas) {
            push_undo();
            if (dd.tool == TOOL_FILL) {
                flood_fill(ccx, ccy);
            } else {
                dd.drawing = 1;
                dd.last_x = ccx;
                dd.last_y = ccy;
                disc(ccx, ccy, SIZES[dd.size], ink());
            }
        } else {
            /* palette swatches */
            for (int i = 0; i < 16; i++) {
                int sx = px + 20 + i * 30, sy = py + 48;
                if (dd.cx >= sx && dd.cx < sx + 26 && dd.cy >= sy && dd.cy < sy + 26) {
                    dd.color = i;
                    if (dd.tool == TOOL_ERASE) dd.tool = TOOL_DRAW;
                }
            }
            for (int i = 0; i < 4; i++) {
                int sx = px + 20 + i * 36, sy = py + 80;
                if (dd.cx >= sx && dd.cx < sx + 32 && dd.cy >= sy && dd.cy < sy + 28) dd.size = i;
            }
            int b = button_at(dd.cx, dd.cy, px, py, pw, ph);
            if (b >= 0) press_button(b);
        }
    } else if ((in->held & BTN_A) && dd.drawing) {
        int x = ccx, y = ccy;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= CANVAS_W) x = CANVAS_W - 1;
        if (y >= CANVAS_H) y = CANVAS_H - 1;
        line(dd.last_x, dd.last_y, x, y);
        dd.last_x = x;
        dd.last_y = y;
    }
    if (!(in->held & BTN_A)) dd.drawing = 0;
}

void doodle_draw(int frame)
{
    (void)frame;
    int px, py, pw, ph, cvx, cvy;
    geometry(&px, &py, &pw, &ph, &cvx, &cvy);
    ui_dim();
    ui_panel(px, py, pw, ph, NULL, "Draw a doodle");

    for (int i = 0; i < 16; i++) {
        int sx = px + 20 + i * 30, sy = py + 48;
        gfx_fill(sx - 2, sy - 2, 30, 30, i == dd.color && dd.tool != TOOL_ERASE ? T.accent : T.input_border);
        gfx_fill(sx, sy, 26, 26, COLORS[i]);
    }
    for (int i = 0; i < 4; i++) {
        int sx = px + 20 + i * 36, sy = py + 80;
        gfx_frame(sx, sy, 32, 28, RADIUS, T.elev, i == dd.size ? T.accent : T.input_border, i == dd.size ? 2 : 1);
        gfx_circle(sx + 16, sy + 14, SIZES[i] / 2 > 0 ? SIZES[i] / 2 : 1, T.text);
    }
    int tx = px + 20 + 4 * 36 + 16;
    for (int i = 0; i < 5; i++) {
        int active = i == dd.tool;
        ui_button(tx + i * 64, py + 80, 60, 28, TOOL_NAMES[i], 0, active, 0);
    }

    gfx_fill(cvx - 1, cvy - 1, CANVAS_W + 2, CANVAS_H + 2, T.input_border);
    gfx_blit(dd.img, cvx, cvy);

    int by = py + ph - 40;
    text_draw(FONT_REGULAR, 12, px + 20, by + 8, T.muted, "Touch or A draw   B cancel   + done   Y undo");
    ui_button(px + pw - 20 - 100 - 8 - 90, by, 90, 28, "Cancel", 0, 0, 0);
    ui_button(px + pw - 20 - 100, by, 100, 28, "Use doodle", 1, 0, 0);

    /* brush outline on the canvas, a crosshair elsewhere (the pointer itself is drawn by the main loop) */
    int ccx = dd.cx - cvx, ccy = dd.cy - cvy;
    if (ccx >= 0 && ccy >= 0 && ccx < CANVAS_W && ccy < CANVAS_H && dd.tool != TOOL_FILL) {
        gfx_ring(dd.cx, dd.cy, SIZES[dd.size] / 2 + 1, 1, dd.tool == TOOL_ERASE ? 0x888888 : 0x000000);
    }
    gfx_fill(dd.cx - 5, dd.cy, 11, 1, T.accent);
    gfx_fill(dd.cx, dd.cy - 5, 1, 11, T.accent);
}
