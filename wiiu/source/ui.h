#pragma once
#include "platform.h"
#include "gfx.h"

/* Theme tokens copied from pictocard.net (public/css/main.css). */
typedef struct {
    u32 bg, bg2, elev, text, muted, accent, accent_hover, accent_active, hr, danger, success;
    u32 input_bg, input_border, shadow, header, tab_bg, tab_text, tab_in_bg, tab_in_text, grid, hover;
    u32 alert_err_bg, alert_err_border, alert_ok_bg, alert_ok_border;
} theme_t;

extern theme_t T;
extern image_t *img_logo, *img_stripe;

#define RADIUS 5

void ui_init(void);
void ui_set_theme(int light);

void ui_header(int light);
void ui_footer_hints(const char *const *pairs, int count); /* "A","Select","B","Back",... */
int ui_panel(int x, int y, int w, int h, const char *step, const char *title); /* returns content top */
void ui_label(int x, int y, const char *label, const char *right);
void ui_input(int x, int y, int w, int h, const char *value, const char *placeholder, int focused, int multiline, int caret);
void ui_button(int x, int y, int w, int h, const char *text, int primary, int focused, int disabled);
void ui_focus_ring(int x, int y, int w, int h);
void ui_tab(int x, int y, int w, int h, const char *text, int active, int focused);
void ui_select(int x, int y, int w, int h, const char *value, u32 swatch, int has_swatch, int focused, int disabled);
void ui_alert(int x, int y, int w, const char *text, int error);
int ui_alert_height(int w, const char *text);
void ui_spinner(int cx, int cy, int r, u32 color, int frame);
void ui_wrapped(int font, int size, int x, int y, int w, u32 color, const char *text, int max_lines);
int ui_wrapped_height(int font, int size, int w, const char *text, int max_lines);
void ui_dim(void);
void ui_radio(int x, int y, const char *text, int checked, int focused);
