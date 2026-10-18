#pragma once
#include <gctypes.h>

/* Text in the site's fonts: Rodin NTLG Pro DB / B, and Pictochat for the
 * step numbers. Strings are UTF-8. y is the top of the line box. */
enum { FONT_REGULAR, FONT_BOLD, FONT_PIXEL, FONT_COUNT };

void font_init(void);
int font_line_height(int font, int size);
int text_width(int font, int size, const char *s);
int text_width_n(int font, int size, const char *s, int bytes);
int text_draw(int font, int size, int x, int y, u32 color, const char *s);
int text_draw_n(int font, int size, int x, int y, u32 color, const char *s, int bytes);
/* Truncate with an ellipsis to fit max_w */
void text_draw_fit(int font, int size, int x, int y, int max_w, u32 color, const char *s);
void text_draw_center(int font, int size, int x, int y, int w, u32 color, const char *s);

/* Word-wrap into at most max_lines; starts[i]/lens[i] are byte ranges. */
int text_wrap(int font, int size, const char *s, int max_w, int *starts, int *lens, int max_lines);

u32 utf8_next(const char **s);
int utf8_prev(const char *s, int pos);
int utf8_encode(u32 cp, char *out);
