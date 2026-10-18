/* A grid of pictures to choose from, like the letterhead gallery on
 * pictocard.net. The first cell is "none" (value -1). */
#include <stdio.h>
#include <string.h>
#include "gallery.h"
#include "font.h"
#include "input.h"
#include "ui.h"
#include "scroll.h"

#define CELL_W 128
#define CELL_H 128 /* thumbnail, then two lines for the name */
#define THUMB_W 120
#define THUMB_H 88
#define GAP 10

static struct {
    int active;
    const char *title, *none_label;
    image_t *const *thumbs;
    const char *const *names, *const *groups;
    int count; /* cells, including "none" */
    int sel, scroll; /* scroll: this frame's offset, from sc */
    scroller_t sc;
    gallery_cb cb;
} g;

static void geom(int *x, int *y, int *w, int *h, int *cols, int *gx, int *gy, int *gh)
{
    *x = 24;
    *y = 64;
    *w = SW - 48;
    *h = SH - 50 - *y;
    *cols = (*w - 28 + GAP) / (CELL_W + GAP);
    if (*cols < 1) *cols = 1;
    int grid_w = *cols * (CELL_W + GAP) - GAP;
    *gx = *x + (*w - grid_w) / 2;
    *gy = *y + 54;
    *gh = *h - 54 - 34; /* room for the selected name at the bottom */
}

void gallery_open(const char *title, const char *none_label, image_t *const *thumbs, const char *const *names,
                  const char *const *groups, int count, int selected, gallery_cb cb)
{
    g.active = 1;
    g.title = title;
    g.none_label = none_label;
    g.thumbs = thumbs;
    g.names = names;
    g.groups = groups;
    g.count = count + 1;
    g.sel = selected + 1;
    if (g.sel < 0 || g.sel >= g.count) g.sel = 0;
    g.cb = cb;
    int x, y, w, h, cols, gx, gy, gh;
    geom(&x, &y, &w, &h, &cols, &gx, &gy, &gh);
    int row_top = (g.sel / cols) * (CELL_H + GAP);
    memset(&g.sc, 0, sizeof g.sc);
    g.sc.pos = g.sc.target = row_top + CELL_H > gh ? row_top + CELL_H - gh : 0;
    g.scroll = (int)g.sc.pos;
}

int gallery_active(void) { return g.active; }
int gallery_hover(void) { return g.sel; }

static int content_h(int cols) { return ((g.count + cols - 1) / cols) * (CELL_H + GAP) - GAP; }

static void keep_visible(int cols, int gh)
{
    int top = (g.sel / cols) * (CELL_H + GAP);
    scroller_reveal(&g.sc, top, top + CELL_H, gh, content_h(cols) - gh);
}

void gallery_update(void *vin)
{
    input_t *in = vin;
    int x, y, w, h, cols, gx, gy, gh;
    geom(&x, &y, &w, &h, &cols, &gx, &gy, &gh);
    key_event ev;
    while (input_key(&ev)) {
        if (ev.code == KEY_UP) in->down |= BTN_UP;
        else if (ev.code == KEY_DOWN) in->down |= BTN_DOWN;
        else if (ev.code == KEY_LEFT) in->down |= BTN_LEFT;
        else if (ev.code == KEY_RIGHT) in->down |= BTN_RIGHT;
        else if (ev.code == KEY_ENTER || ev.code == KEY_KP_ENTER) in->down |= BTN_A;
        else if (ev.code == KEY_ESC || ev.code == KEY_BACKSPACE) in->down |= BTN_B;
    }
    int moved_sel = 0;
    if (in->down & BTN_LEFT && g.sel > 0) g.sel--, moved_sel = 1;
    if (in->down & BTN_RIGHT && g.sel < g.count - 1) g.sel++, moved_sel = 1;
    if (in->down & BTN_UP && g.sel >= cols) g.sel -= cols, moved_sel = 1;
    if (in->down & BTN_DOWN) {
        if (g.sel + cols < g.count) g.sel += cols;
        else if (g.sel / cols < (g.count - 1) / cols) g.sel = g.count - 1;
        moved_sel = 1;
    }
    if (moved_sel) keep_visible(cols, gh);

    int hover = -1;
    if (in->pointer && in->px >= gx - GAP / 2 && in->py >= gy && in->py < gy + gh) {
        int cx = (in->px - gx) / (CELL_W + GAP), cy = (in->py - gy + g.scroll) / (CELL_H + GAP);
        int ix = in->px - gx - cx * (CELL_W + GAP), iy = in->py - gy + g.scroll - cy * (CELL_H + GAP);
        int i = cy * cols + cx;
        if (cx < cols && ix < CELL_W && iy < CELL_H && i < g.count) hover = i;
    }
    if (in->moved && hover >= 0 && !g.sc.dragging) g.sel = hover;
    /* B-drag, edge scrolling and easing (scroll.c); a B tap cancels */
    scroller_update(&g.sc, in, x, gy, w, gh, content_h(cols) - gh);
    g.scroll = scroller_px(&g.sc);

    if (in->down & BTN_A) {
        if (in->pointer && hover >= 0) g.sel = hover;
        g.active = 0;
        if (g.cb) g.cb(g.sel - 1);
    } else if (g.sc.b_tap) {
        g.active = 0;
    }
}

/* a plain letter, drawn instead of a thumbnail */
static void draw_plain(int x, int y)
{
    gfx_fill(x, y, THUMB_W, THUMB_H, 0xFFFFFF);
    gfx_fill(x, y, THUMB_W, 14, 0x8fd3f5);
    for (int i = 0; i < 4; i++) gfx_fill(x + 10, y + 30 + i * 13, THUMB_W - 20, 1, 0xc8d6de);
}

void gallery_draw(int frame)
{
    if (!g.active) return;
    int x, y, w, h, cols, gx, gy, gh;
    geom(&x, &y, &w, &h, &cols, &gx, &gy, &gh);
    ui_dim();
    ui_panel(x, y, w, h, NULL, g.title);
    gfx_clip(x + 1, gy - 4, w - 2, gh + 8);
    for (int i = 0; i < g.count; i++) {
        int cx = gx + (i % cols) * (CELL_W + GAP), cy = gy + (i / cols) * (CELL_H + GAP) - g.scroll;
        if (cy + CELL_H < gy - 4 || cy > gy + gh + 4) continue;
        int sel = i == g.sel;
        if (sel) ui_focus_ring(cx, cy, CELL_W, CELL_H);
        gfx_frame(cx, cy, CELL_W, CELL_H, RADIUS, sel ? T.hover : T.elev, sel ? T.accent : T.input_border, sel ? 2 : 1);
        int tx = cx + (CELL_W - THUMB_W) / 2, ty = cy + 4;
        const image_t *im = i ? g.thumbs[i - 1] : NULL;
        if (!i) draw_plain(tx, ty);
        else if (im) gfx_stretch(im, tx, ty, THUMB_W, THUMB_H);
        else {
            gfx_fill(tx, ty, THUMB_W, THUMB_H, T.bg2);
            ui_spinner(tx + THUMB_W / 2, ty + THUMB_H / 2, 8, T.accent, frame + i * 3);
        }
        const char *name = i ? g.names[i - 1] : g.none_label;
        ui_wrapped(FONT_REGULAR, 11, cx + 6, cy + THUMB_H + 6, CELL_W - 12, sel ? T.accent : T.text, name, 2);
    }
    gfx_noclip();
    /* fade the grid's edges while it scrolls */
    if (g.scroll > 0) for (int i = 0; i < 8; i++) gfx_fill_alpha(x + 1, gy - 4 + i, w - 2, 1, T.bg, 220 - i * 26);
    if (g.scroll < content_h(cols) - gh)
        for (int i = 0; i < 8; i++) gfx_fill_alpha(x + 1, gy + gh + 3 - i, w - 2, 1, T.bg, 220 - i * 26);

    char line[160];
    if (g.sel == 0) snprintf(line, sizeof line, "%s", g.none_label);
    else if (g.groups && g.groups[g.sel - 1] && *g.groups[g.sel - 1])
        snprintf(line, sizeof line, "%s  -  %s", g.groups[g.sel - 1], g.names[g.sel - 1]);
    else snprintf(line, sizeof line, "%s", g.names[g.sel - 1]);
    gfx_fill(x + 1, y + h - 34, w - 2, 1, T.input_border);
    text_draw_fit(FONT_BOLD, 13, x + 14, y + h - 26, w - 28, T.text, line);
    static const char *const hints[] = { "A", "Choose", "B", "Cancel" };
    ui_footer_hints(hints, 2);
}
