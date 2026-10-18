#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "font.h"
#include "image.h"
#include "logo_png.h"
#include "stripe_png.h"

theme_t T;
image_t *img_logo, *img_stripe;

static const theme_t DARK = {
    .bg = 0x18232b, .bg2 = 0x24323c, .elev = 0x1d2b35, .text = 0xedf4fa, .muted = 0xbdcbd5,
    .accent = 0x84dbfc, .accent_hover = 0xb2eaff, .accent_active = 0x67c1e3, .hr = 0x577080,
    .danger = 0xff9eaa, .success = 0x92dbb3, .input_bg = 0x24323c, .input_border = 0x577080,
    .shadow = 0x0f171d, .header = 0x23313b, .tab_bg = 0x2d4655, .tab_text = 0x84dbfc,
    .tab_in_bg = 0x1d2b35, .tab_in_text = 0xbdcbd5, .grid = 0x314650, .hover = 0x2d4655,
    .alert_err_bg = 0x3a2b35, .alert_err_border = 0x7a3a44, .alert_ok_bg = 0x1f3a37, .alert_ok_border = 0x22604a,
};
static const theme_t LIGHT = {
    .bg = 0xe9edf0, .bg2 = 0xfafbfc, .elev = 0xf0f3f5, .text = 0x24323b, .muted = 0x526472,
    .accent = 0x006f9b, .accent_hover = 0x005879, .accent_active = 0x004863, .hr = 0xaabac4,
    .danger = 0xb72e3e, .success = 0x237447, .input_bg = 0xfafbfc, .input_border = 0xaabac4,
    .shadow = 0xb6c2c9, .header = 0xf7f9fb, .tab_bg = 0xd8f0fb, .tab_text = 0x006f9b,
    .tab_in_bg = 0xf0f3f5, .tab_in_text = 0x526472, .grid = 0xd9e3e9, .hover = 0xd8f0fb,
    .alert_err_bg = 0xf3d9dc, .alert_err_border = 0xe3a3aa, .alert_ok_bg = 0xd3eadb, .alert_ok_border = 0x9fd1b2,
};

void ui_set_theme(int light) { T = light ? LIGHT : DARK; }

void ui_init(void)
{
    image_t *logo = image_decode(logo_png, logo_png_size, 1024);
    img_logo = logo ? image_fit(logo, 200, 40) : NULL;
    image_free(logo);
    img_stripe = image_decode(stripe_png, stripe_png_size, 64);
    ui_set_theme(0);
}

static void stripe(int x, int y, int w, int h)
{
    gfx_tile(img_stripe, x, y, w, h, 16);
}

void ui_header(int light)
{
    gfx_fill(0, 0, SW, 52, T.header);
    gfx_blit(img_logo, 24, 6);
    int bx = SW - 24 - 92;
    gfx_frame(bx, 13, 92, 26, RADIUS, T.elev, T.input_border, 1);
    text_draw_center(FONT_REGULAR, 12, bx, 18, 92, T.text, light ? "Dark mode" : "Light mode");
    int nw = text_width(FONT_BOLD, 14, "Create a card");
    text_draw(FONT_BOLD, 14, bx - 22 - nw, 17, T.accent, "Create a card");
    gfx_fill(0, 52, SW, 1, T.input_border);
    gfx_fill(0, 53, SW, 1, 0x56899f);
    stripe(0, 54, SW, 10);
    gfx_fill(0, 64, SW, 1, 0x56899f);
}

void ui_footer_hints(const char *const *pairs, int count)
{
    int x = 24, y = SH - 34;
    gfx_fill(0, SH - 40, SW, 40, T.bg);
    for (int i = 0; i + 1 < count * 2; i += 2) {
        const char *b = pairs[i], *t = pairs[i + 1];
        int bw = text_width(FONT_BOLD, 11, b) + 12;
        if (bw < 20) bw = 20;
        gfx_frame(x, y + 2, bw, 20, 10, T.elev, T.input_border, 1);
        text_draw_center(FONT_BOLD, 11, x, y + 3, bw, T.text, b);
        x += bw + 6;
        x = text_draw(FONT_REGULAR, 12, x, y + 2, T.muted, t) + 16;
    }
}

int ui_panel(int x, int y, int w, int h, const char *step, const char *title)
{
    gfx_round(x, y + 3, w, h, RADIUS, T.shadow);
    gfx_frame(x, y, w, h, RADIUS, T.bg2, T.input_border, 1);
    gfx_round(x + 1, y + 1, w - 2, 40, RADIUS - 1, T.elev);
    gfx_fill(x + 1, y + 8, w - 2, 33, T.elev);
    stripe(x + 2, y + 1, w - 4, 5);
    gfx_fill(x + 1, y + 41, w - 2, 1, T.input_border);
    int tx = x + 14;
    if (step) {
        gfx_frame(tx, y + 12, 24, 23, 0, T.tab_bg, T.accent, 1);
        text_draw_center(FONT_PIXEL, 16, tx, y + 11, 25, T.accent, step);
        tx += 33;
    }
    text_draw(FONT_BOLD, 15, tx, y + 12, T.text, title);
    return y + 42;
}

void ui_label(int x, int y, const char *label, const char *right)
{
    text_draw(FONT_BOLD, 13, x, y, T.muted, label);
    if (right) {
        int w = text_width(FONT_REGULAR, 12, right);
        (void)w;
    }
}

void ui_focus_ring(int x, int y, int w, int h)
{
    gfx_round(x - 3, y - 3, w + 6, h + 6, RADIUS + 3, gfx_mix(T.bg2, T.accent, 70));
}

void ui_input(int x, int y, int w, int h, const char *value, const char *placeholder, int focused, int multiline, int caret)
{
    if (focused) ui_focus_ring(x, y, w, h);
    gfx_frame(x, y, w, h, RADIUS, T.input_bg, focused ? T.accent : T.input_border, focused ? 2 : 1);
    int empty = !value || !*value;
    const char *s = empty ? placeholder : value;
    u32 color = empty ? T.muted : T.text;
    if (!s) s = "";
    if (multiline) {
        int starts[16], lens[16];
        int lines = (h - 12) / 22;
        int n = text_wrap(FONT_REGULAR, 14, s, w - 20, starts, lens, 16);
        int first = n > lines ? n - lines : 0;
        for (int i = first; i < n; i++)
            text_draw_n(FONT_REGULAR, 14, x + 10, y + 5 + (i - first) * 22, color, s + starts[i], lens[i]);
    } else {
        gfx_clip(x + 2, y, w - 4, h);
        int tw = text_width(FONT_REGULAR, 14, s);
        int tx = x + 10;
        if (!empty && tw > w - 22) tx = x + w - 12 - tw; /* keep the end of long text visible */
        int end = text_draw(FONT_REGULAR, 14, tx, y + (h - 22) / 2, color, s);
        if (caret && focused) gfx_fill(empty ? x + 10 : end + 1, y + 8, 2, h - 16, T.accent);
        gfx_noclip();
    }
}

void ui_button(int x, int y, int w, int h, const char *text, int primary, int focused, int disabled)
{
    if (focused) ui_focus_ring(x, y, w, h);
    if (primary) {
        gfx_round(x, y, w, h, RADIUS, 0x367f9e);
        gfx_clip(x + 1, y + 1, w - 2, h - 2);
        gfx_stretch(img_stripe, x + 1, y + 1, 16, h - 2);
        for (int i = 1; i < w - 1; i += 16) gfx_stretch(img_stripe, x + i, y + 1, 16, h - 2);
        gfx_noclip();
        if (disabled) gfx_fill_alpha(x + 1, y + 1, w - 2, h - 2, T.bg, 110);
        /* a trailing " ->" is drawn as an arrow, like the site's &rarr; */
        char label[96];
        snprintf(label, sizeof label, "%s", text);
        size_t n = strlen(label);
        int arrow = n > 3 && !strcmp(label + n - 3, " ->");
        if (arrow) label[n - 3] = 0;
        int tw = text_width(FONT_BOLD, 15, label) + (arrow ? 22 : 0);
        int tx = x + (w - tw) / 2, ty = y + (h - 23) / 2;
        text_draw(FONT_BOLD, 15, tx, ty, 0x173644, label);
        if (arrow) {
            int ax = tx + tw - 14, ay = y + h / 2;
            gfx_fill(ax, ay - 1, 12, 2, 0x173644);
            for (int i = 0; i < 5; i++) gfx_fill(ax + 7 + i, ay - 5 + i, 2, 10 - 2 * i, 0x173644);
        }
    } else {
        gfx_frame(x, y, w, h, RADIUS, focused ? T.hover : T.elev, focused ? T.accent : T.input_border, 1);
        text_draw_center(FONT_BOLD, 14, x, y + (h - 22) / 2, w, disabled ? T.muted : T.text, text);
    }
}

void ui_tab(int x, int y, int w, int h, const char *text, int active, int focused)
{
    if (focused) ui_focus_ring(x, y, w, h);
    gfx_frame(x, y, w, h, RADIUS, active ? T.tab_bg : T.tab_in_bg, active ? T.accent : T.input_border, 1);
    text_draw_center(FONT_BOLD, 15, x, y + (h - 23) / 2, w, active ? T.tab_text : T.tab_in_text, text);
}

void ui_select(int x, int y, int w, int h, const char *value, u32 swatch, int has_swatch, int focused, int disabled)
{
    if (focused) ui_focus_ring(x, y, w, h);
    gfx_frame(x, y, w, h, RADIUS, T.input_bg, focused ? T.accent : T.input_border, focused ? 2 : 1);
    int tx = x + 10;
    if (has_swatch) {
        gfx_frame(tx, y + (h - 18) / 2, 18, 18, 4, swatch, T.input_border, 1);
        gfx_round(tx + 1, y + (h - 18) / 2 + 1, 16, 16, 3, swatch);
        tx += 26;
    }
    text_draw_fit(FONT_REGULAR, 14, tx, y + (h - 22) / 2, w - (tx - x) - 44, disabled ? T.muted : T.text, value);
    /* ‹ › arrows: Left/Right change the value */
    int ay = y + h / 2;
    u32 c = focused ? T.accent : T.muted;
    for (int i = 0; i < 5; i++) {
        gfx_fill(x + w - 30 + i, ay - i, 1, 2 * i + 1, c);
        gfx_fill(x + w - 12 - i, ay - i, 1, 2 * i + 1, c);
    }
}

int ui_alert_height(int w, const char *text)
{
    return ui_wrapped_height(FONT_BOLD, 13, w - 28, text, 4) + 20;
}

void ui_alert(int x, int y, int w, const char *text, int error)
{
    int h = ui_alert_height(w, text);
    gfx_frame(x, y, w, h, RADIUS, error ? T.alert_err_bg : T.alert_ok_bg, error ? T.alert_err_border : T.alert_ok_border, 1);
    ui_wrapped(FONT_BOLD, 13, x + 14, y + 10, w - 28, error ? T.danger : T.success, text, 4);
}

void ui_spinner(int cx, int cy, int r, u32 color, int frame)
{
    static const int dx[8] = { 0, 70, 100, 70, 0, -70, -100, -70 };
    static const int dy[8] = { -100, -70, 0, 70, 100, 70, 0, -70 };
    int head = (frame / 5) % 8;
    for (int i = 0; i < 8; i++) {
        int age = (head - i + 8) % 8;
        gfx_circle(cx + dx[i] * r / 100, cy + dy[i] * r / 100, r / 4 + 1, gfx_mix(T.bg2, color, 255 - age * 28));
    }
}

int ui_wrapped_height(int font, int size, int w, const char *text, int max_lines)
{
    int starts[16], lens[16];
    int n = text_wrap(font, size, text, w, starts, lens, max_lines > 16 ? 16 : max_lines);
    return n * font_line_height(font, size);
}

void ui_wrapped(int font, int size, int x, int y, int w, u32 color, const char *text, int max_lines)
{
    int starts[16], lens[16];
    int n = text_wrap(font, size, text, w, starts, lens, max_lines > 16 ? 16 : max_lines);
    for (int i = 0; i < n; i++)
        text_draw_n(font, size, x, y + i * font_line_height(font, size), color, text + starts[i], lens[i]);
}

void ui_dim(void)
{
    gfx_fill_alpha(0, 0, SW, SH, 0x05090c, 150);
}

void ui_radio(int x, int y, const char *text, int checked, int focused)
{
    if (focused) gfx_circle(x + 9, y + 11, 13, gfx_mix(T.bg2, T.accent, 70));
    gfx_circle(x + 9, y + 11, 9, checked ? T.accent : T.input_border);
    gfx_circle(x + 9, y + 11, 8, T.input_bg);
    if (checked) gfx_circle(x + 9, y + 11, 5, T.accent);
    text_draw(FONT_REGULAR, 14, x + 24, y, T.text, text);
}
