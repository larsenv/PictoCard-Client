/* Modal lists: choosing from options (fonts, letterheads, Miis, colours...)
 * and browsing the SD card / USB drive for a picture. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include "app.h"
#include "input.h"
#include "font.h"
#include "ui.h"
#include "image.h"
#include "util.h"
#include "scroll.h"

#define ROW_H 30
#define MAX_ITEMS 512

static struct {
    int active;
    const char *title;
    pick_item_t *items;
    int count;
    int sel;
    int scroll; /* pixels, this frame's offset from sc */
    scroller_t sc;
    pick_cb cb;
    pick_preview_cb preview;
    void (*closed)(void);
    image_t *thumb;
    int thumb_for, idle, last_sel;
} pk;

static void picker_close(void)
{
    if (!pk.active && !pk.thumb && !pk.closed) return;
    pk.active = 0;
    image_free(pk.thumb);
    pk.thumb = NULL;
    void (*closed)(void) = pk.closed;
    pk.closed = NULL;
    if (closed) closed();
}

static int panel_geom(int *x, int *y, int *w, int *h)
{
    *w = SW > 700 ? 520 : 440;
    *h = SH - 70 - 50;
    *x = (SW - *w) / 2;
    *y = 74;
    return (*h - 42 - 24) / ROW_H; /* visible rows */
}

static int selectable(pick_item_t *items, int i) { return !items[i].header; }

static void step(pick_item_t *items, int count, int *sel, int dir)
{
    int i = *sel;
    for (int n = 0; n < count; n++) {
        i += dir;
        if (i < 0 || i >= count) return;
        if (selectable(items, i)) {
            *sel = i;
            return;
        }
    }
}

static int list_max(int count, int visible) { return count * ROW_H - visible * ROW_H; }

/* ease the list so the selected row shows, with half a row of context */
static void keep_visible(scroller_t *sc, int sel, int count, int visible)
{
    int top = sel * ROW_H - (sel > 0 ? ROW_H / 2 : 0), bottom = sel * ROW_H + ROW_H + ROW_H / 2;
    scroller_reveal(sc, top, bottom, visible * ROW_H, list_max(count, visible));
}

/* B-drag, edge scrolling and easing for a list (scroll.c) */
static int list_scroll(scroller_t *sc, input_t *in, int count, int visible)
{
    int x, y, w, h;
    panel_geom(&x, &y, &w, &h);
    scroller_update(sc, in, x, y + 42 + 12, w, visible * ROW_H, list_max(count, visible));
    return scroller_px(sc);
}

/* Shared list drawing; returns the hovered row or -1 */
static void draw_list(const char *title, pick_item_t *items, int count, int sel, int scroll, const char *empty,
                      image_t *thumb)
{
    int x, y, w, h;
    int visible = panel_geom(&x, &y, &w, &h);
    ui_dim();
    int top = ui_panel(x, y, w, h, NULL, title);
    int lx = x + 12, lw = w - 24 - (thumb ? 0 : 0), ly = top + 12;
    gfx_clip(x + 1, ly, w - 2, visible * ROW_H);
    for (int i = 0; i < count; i++) {
        int ry = ly + i * ROW_H - scroll;
        if (ry + ROW_H < ly || ry > ly + visible * ROW_H) continue;
        pick_item_t *it = &items[i];
        if (it->header) {
            text_draw(FONT_BOLD, 12, lx + 4, ry + 8, T.accent, it->label);
            gfx_fill(lx + 8 + text_width(FONT_BOLD, 12, it->label), ry + 17, lw - 20 - text_width(FONT_BOLD, 12, it->label), 1, T.hr);
            continue;
        }
        if (i == sel) gfx_frame(lx, ry + 1, lw, ROW_H - 2, RADIUS, T.tab_bg, T.accent, 1);
        int tx = lx + 10;
        if (it->icon) {
            gfx_round(tx, ry + 3, 24, 24, 4, T.bg2);
            gfx_stretch(it->icon, tx, ry + 3, 24, 24);
            tx += 32;
        } else if (it->has_color) {
            gfx_frame(tx, ry + 6, 18, 18, 4, it->color, T.input_border, 1);
            gfx_round(tx + 1, ry + 7, 16, 16, 3, it->color);
            tx += 26;
        }
        text_draw_fit(FONT_REGULAR, 14, tx, ry + 4, lw - (tx - lx) - 10, i == sel ? T.tab_text : T.text, it->label);
    }
    gfx_noclip();
    if (!count) text_draw_center(FONT_REGULAR, 14, x, ly + 20, w, T.muted, empty);
    /* scroll bar */
    int total = count * ROW_H, view = visible * ROW_H;
    if (total > view) {
        int bh = view * view / total, by = ly + (view - bh) * scroll / (total - view);
        gfx_round(x + w - 8, by, 4, bh, 2, T.hr);
    }
}

static int hover_row(input_t *in, int scroll, int count)
{
    int x, y, w, h;
    int visible = panel_geom(&x, &y, &w, &h);
    int ly = y + 42 + 12;
    if (!in->pointer || in->px < x || in->px > x + w || in->py < ly || in->py >= ly + visible * ROW_H) return -1;
    int i = (in->py - ly + scroll) / ROW_H;
    return i < count ? i : -1;
}

/* ── List picker ─────────────────────────────────────────────────────────── */

int picker_active(void) { return pk.active; }
int picker_hover(void) { return pk.sel; }

void picker_open_preview(const char *title, pick_item_t *items, int count, int selected_value, pick_cb cb,
                         pick_preview_cb preview, void (*closed)(void))
{
    picker_open(title, items, count, selected_value, cb);
    pk.preview = preview;
    pk.closed = closed;
    pk.thumb_for = -1;
    pk.last_sel = -1;
}

void picker_open(const char *title, pick_item_t *items, int count, int selected_value, pick_cb cb)
{
    memset(&pk, 0, sizeof pk);
    pk.active = 1;
    pk.title = title;
    pk.items = items;
    pk.count = count;
    pk.cb = cb;
    pk.sel = -1;
    for (int i = 0; i < count; i++)
        if (!items[i].header && (pk.sel < 0 || items[i].value == selected_value)) {
            if (items[i].value == selected_value || pk.sel < 0) pk.sel = i;
            if (items[i].value == selected_value) break;
        }
    if (pk.sel < 0) pk.sel = 0;
    int x, y, w, h, visible = panel_geom(&x, &y, &w, &h);
    int start = pk.sel * ROW_H - (visible / 2) * ROW_H, max = list_max(count, visible);
    if (start > max) start = max;
    if (start < 0) start = 0;
    pk.sc.pos = pk.sc.target = start;
    pk.scroll = start;
}

void picker_update(void *vin)
{
    input_t *in = vin;
    int x, y, w, h;
    int visible = panel_geom(&x, &y, &w, &h);
    key_event ev;
    while (input_key(&ev)) {
        if (ev.code == KEY_UP) in->down |= BTN_UP;
        else if (ev.code == KEY_DOWN) in->down |= BTN_DOWN;
        else if (ev.code == KEY_ENTER || ev.code == KEY_KP_ENTER) in->down |= BTN_A;
        else if (ev.code == KEY_ESC || ev.code == KEY_BACKSPACE) in->down |= BTN_B;
        else if (ev.code == KEY_PAGEDOWN) in->down |= BTN_RIGHT;
        else if (ev.code == KEY_PAGEUP) in->down |= BTN_LEFT;
    }
    if (in->moved && !pk.sc.dragging) {
        int r = hover_row(in, pk.scroll, pk.count);
        if (r >= 0 && selectable(pk.items, r)) pk.sel = r;
    }
    if (in->down & BTN_UP) step(pk.items, pk.count, &pk.sel, -1);
    if (in->down & BTN_DOWN) step(pk.items, pk.count, &pk.sel, 1);
    if (in->down & BTN_LEFT)
        for (int i = 0; i < visible - 1; i++) step(pk.items, pk.count, &pk.sel, -1);
    if (in->down & BTN_RIGHT)
        for (int i = 0; i < visible - 1; i++) step(pk.items, pk.count, &pk.sel, 1);
    if (in->down & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT)) keep_visible(&pk.sc, pk.sel, pk.count, visible);
    pk.scroll = list_scroll(&pk.sc, in, pk.count, visible);
    /* preview of the highlighted entry once the selection settles */
    if (pk.preview) {
        if (pk.sel != pk.last_sel) {
            pk.last_sel = pk.sel;
            pk.idle = 0;
        } else {
            pk.idle++;
        }
        if (pk.idle == 10 && pk.count && pk.thumb_for != pk.sel) {
            image_free(pk.thumb);
            pk.thumb = pk.preview(pk.items[pk.sel].value);
            pk.thumb_for = pk.sel;
        }
    }
    if (in->down & BTN_A) {
        if (in->pointer && hover_row(in, pk.scroll, pk.count) < 0 &&
            (in->px < x || in->px > x + w || in->py < y || in->py > y + h)) {
            picker_close(); /* clicked outside */
            return;
        }
        /* the callback runs while the list's resources (preview, Message Board data) are still open */
        pick_cb cb = pk.cb;
        int value = pk.count ? pk.items[pk.sel].value : 0, any = pk.count && cb;
        void (*closed)(void) = pk.closed;
        image_t *thumb = pk.thumb;
        pk.closed = NULL;
        pk.thumb = NULL;
        pk.active = 0;
        if (any) cb(value);
        image_free(thumb);
        if (closed) closed();
    } else if (pk.sc.b_tap) {
        picker_close();
    }
}

void picker_draw(int frame)
{
    (void)frame;
    draw_list(pk.title, pk.items, pk.count, pk.sel, pk.scroll, "Nothing to choose from.", NULL);
    if (pk.thumb && pk.thumb_for == pk.sel) {
        int x, y, w, h;
        panel_geom(&x, &y, &w, &h);
        int tx = x + w - pk.thumb->w - 20, ty = y + h - pk.thumb->h - 18;
        gfx_round(tx - 5, ty - 3, pk.thumb->w + 10, pk.thumb->h + 10, RADIUS, T.shadow);
        gfx_frame(tx - 5, ty - 5, pk.thumb->w + 10, pk.thumb->h + 10, RADIUS, 0xFFFFFF, T.input_border, 1);
        gfx_blit(pk.thumb, tx, ty);
    }
    static const char *const hints[] = { "A", "Choose", "B", "Cancel", "<>", "Page" };
    ui_footer_hints(hints, 3);
}

/* ── File browser ────────────────────────────────────────────────────────── */

static struct {
    int active;
    char dir[256];
    pick_item_t items[MAX_ITEMS];
    int is_dir[MAX_ITEMS];
    char names[MAX_ITEMS][96];
    int count;
    int sel, scroll;
    scroller_t sc;
    file_cb cb;
    image_t *thumb;
    int thumb_for;
    int idle;
    char error[96];
} fb;

static char last_dir[256];

static int is_image(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    static const char *const exts[] = { ".jpg", ".jpeg", ".jpe", ".jfif", ".png", ".webp", ".bmp", ".gif" };
    for (unsigned i = 0; i < sizeof exts / sizeof *exts; i++)
        if (!strcasecmp(dot, exts[i])) return 1;
    return 0;
}

static int by_name(const void *a, const void *b)
{
    const int ia = *(const int *)a, ib = *(const int *)b;
    if (fb.is_dir[ia] != fb.is_dir[ib]) return fb.is_dir[ib] - fb.is_dir[ia];
    return strcasecmp(fb.names[ia], fb.names[ib]);
}

static void list_dir(const char *dir)
{
    image_free(fb.thumb);
    fb.thumb = NULL;
    fb.thumb_for = -1;
    fb.count = 0;
    fb.sel = 0;
    fb.scroll = 0;
    memset(&fb.sc, 0, sizeof fb.sc);
    fb.error[0] = 0;
    snprintf(fb.dir, sizeof fb.dir, "%s", dir);

    if (!*dir) { /* device list */
        static const char *devs[] = { "sd:/", "usb:/" };
        static const char *labels[] = { "SD Card", "USB Drive" };
        for (int i = 0; i < 2; i++) {
            DIR *d = opendir(devs[i]);
            if (!d) continue;
            closedir(d);
            snprintf(fb.names[fb.count], 96, "%s", devs[i]);
            snprintf(fb.items[fb.count].label, 96, "%s  (%s)", labels[i], devs[i]);
            fb.is_dir[fb.count] = 1;
            fb.items[fb.count].value = fb.count;
            fb.count++;
        }
        if (!fb.count) snprintf(fb.error, sizeof fb.error, "Insert an SD card or USB drive with pictures.");
        return;
    }

    int order[MAX_ITEMS];
    char names[MAX_ITEMS][96];
    int dirs[MAX_ITEMS];
    int n = 0;
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && n < MAX_ITEMS - 1) {
            if (e->d_name[0] == '.') continue;
            int isdir = e->d_type == DT_DIR;
            if (e->d_type == DT_UNKNOWN) {
                char full[512];
                struct stat st;
                snprintf(full, sizeof full, "%s%s", dir, e->d_name);
                isdir = !stat(full, &st) && S_ISDIR(st.st_mode);
            }
            if (!isdir && !is_image(e->d_name)) continue;
            snprintf(names[n], 96, "%s", e->d_name);
            dirs[n] = isdir;
            n++;
        }
        closedir(d);
    }
    /* ".." first, then folders, then pictures */
    snprintf(fb.names[0], 96, "..");
    snprintf(fb.items[0].label, 96, ".. (back)");
    fb.is_dir[0] = 1;
    fb.items[0].value = 0;
    for (int i = 0; i < n; i++) {
        snprintf(fb.names[i + 1], 96, "%s", names[i]);
        fb.is_dir[i + 1] = dirs[i];
        order[i] = i + 1;
    }
    qsort(order, n, sizeof(int), by_name);
    char tmp_names[MAX_ITEMS][96];
    int tmp_dirs[MAX_ITEMS];
    for (int i = 0; i < n; i++) {
        snprintf(tmp_names[i], 96, "%s", fb.names[order[i]]);
        tmp_dirs[i] = fb.is_dir[order[i]];
    }
    for (int i = 0; i < n; i++) {
        snprintf(fb.names[i + 1], 96, "%s", tmp_names[i]);
        fb.is_dir[i + 1] = tmp_dirs[i];
        snprintf(fb.items[i + 1].label, 96, "%s%s", tmp_names[i], tmp_dirs[i] ? "/" : "");
        fb.items[i + 1].value = i + 1;
    }
    fb.count = n + 1;
    if (!n) snprintf(fb.error, sizeof fb.error, "No photos in this folder (JPEG, PNG, WebP, GIF or BMP).");
}

int files_active(void) { return fb.active; }
int files_hover(void) { return fb.sel; }

void files_open(file_cb cb)
{
    fb.active = 1;
    fb.cb = cb;
    list_dir(last_dir);
}

static void enter(void)
{
    int i = fb.sel;
    if (i >= fb.count) return;
    char path[512];
    if (!*fb.dir) {
        list_dir(fb.names[i]);
        return;
    }
    if (i == 0) { /* up: "sd:/DCIM/100/" -> "sd:/DCIM/" -> "sd:/" -> device list */
        char up[256];
        snprintf(up, sizeof up, "%s", fb.dir);
        size_t len = strlen(up);
        if (len && up[len - 1] == '/') up[--len] = 0;
        char *slash = strrchr(up, '/');
        if (!slash) list_dir("");
        else {
            slash[1] = 0;
            list_dir(up);
        }
        return;
    }
    snprintf(path, sizeof path, "%s%s%s", fb.dir, fb.names[i], fb.is_dir[i] ? "/" : "");
    if (fb.is_dir[i]) list_dir(path);
    else {
        snprintf(last_dir, sizeof last_dir, "%s", fb.dir);
        fb.active = 0;
        image_free(fb.thumb);
        fb.thumb = NULL;
        if (fb.cb) fb.cb(path);
    }
}

static void up_dir(void)
{
    if (!*fb.dir) {
        fb.active = 0;
        return;
    }
    fb.sel = 0;
    enter();
}

void files_update(void *vin)
{
    input_t *in = vin;
    int x, y, w, h;
    int visible = panel_geom(&x, &y, &w, &h);
    key_event ev;
    while (input_key(&ev)) {
        if (ev.code == KEY_UP) in->down |= BTN_UP;
        else if (ev.code == KEY_DOWN) in->down |= BTN_DOWN;
        else if (ev.code == KEY_ENTER || ev.code == KEY_KP_ENTER) in->down |= BTN_A;
        else if (ev.code == KEY_BACKSPACE) in->down |= BTN_1;
        else if (ev.code == KEY_ESC) in->down |= BTN_B;
    }
    int before = fb.sel;
    if (in->moved && !fb.sc.dragging) {
        int r = hover_row(in, fb.scroll, fb.count);
        if (r >= 0) fb.sel = r;
    }
    if (in->down & BTN_UP) step(fb.items, fb.count, &fb.sel, -1);
    if (in->down & BTN_DOWN) step(fb.items, fb.count, &fb.sel, 1);
    if (in->down & BTN_LEFT)
        for (int i = 0; i < visible - 1; i++) step(fb.items, fb.count, &fb.sel, -1);
    if (in->down & BTN_RIGHT)
        for (int i = 0; i < visible - 1; i++) step(fb.items, fb.count, &fb.sel, 1);
    if (in->down & BTN_DIRS) keep_visible(&fb.sc, fb.sel, fb.count, visible);
    fb.scroll = list_scroll(&fb.sc, in, fb.count, visible);
    if (fb.sel != before) fb.idle = 0;
    else fb.idle++;

    /* thumbnail of the highlighted picture once the selection settles */
    if (fb.idle == 12 && *fb.dir && fb.sel > 0 && !fb.is_dir[fb.sel] && fb.thumb_for != fb.sel) {
        char path[512];
        snprintf(path, sizeof path, "%s%s", fb.dir, fb.names[fb.sel]);
        size_t len;
        unsigned char *data = read_file(path, &len, 48 * 1024 * 1024);
        image_free(fb.thumb);
        fb.thumb = NULL;
        if (data) {
            image_t *img = image_decode(data, len, 200);
            free(data);
            if (img) {
                fb.thumb = image_fit(img, 150, 120);
                image_free(img);
            }
        }
        fb.thumb_for = fb.sel;
    }

    if (in->down & BTN_A) enter();
    else if (in->down & BTN_1) up_dir();
    else if (fb.sc.b_tap) fb.active = 0;
}

void files_draw(int frame)
{
    (void)frame;
    char title[300];
    snprintf(title, sizeof title, "Choose a picture%s%s", *fb.dir ? " - " : "", fb.dir);
    draw_list(title, fb.items, fb.count, fb.sel, fb.scroll, fb.error, NULL);
    int x, y, w, h;
    panel_geom(&x, &y, &w, &h);
    if (fb.error[0] && fb.count <= 1) text_draw_center(FONT_REGULAR, 13, x, y + h - 60, w, T.muted, fb.error);
    if (fb.thumb && fb.thumb_for == fb.sel) {
        int tx = x + w - fb.thumb->w - 20, ty = y + h - fb.thumb->h - 18;
        gfx_round(tx - 5, ty - 3, fb.thumb->w + 10, fb.thumb->h + 10, RADIUS, T.shadow);
        gfx_frame(tx - 5, ty - 5, fb.thumb->w + 10, fb.thumb->h + 10, RADIUS, 0xFFFFFF, T.input_border, 1);
        gfx_blit(fb.thumb, tx, ty);
    }
    static const char *const hints[] = { "A", "Open", "1", "Up a folder", "B", "Cancel" };
    ui_footer_hints(hints, 3);
}
