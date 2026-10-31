#pragma once
#include "gfx.h"

/* gallery.c — a grid of pictures to pick from (letterheads, Message Board
 * pictures). With a none_label the first cell is "none" and reports -1; the
 * others report their index. A NULL thumbnail is drawn as a spinner, so
 * thumbnails can arrive while the gallery is open. */
typedef void (*gallery_cb)(int value);
void gallery_open(const char *title, const char *none_label, image_t *const *thumbs, const char *const *names,
                  const char *const *groups, int count, int selected, gallery_cb cb);
int gallery_active(void);
/* while the gallery is empty, this is shown in its place (e.g. "Looking for pictures...") */
void gallery_set_status(const char *text);
/* the number of pictures changed (thumbs[] and names[] must have that many entries) */
void gallery_set_count(int count);
/* called once whenever the gallery closes, after the pick callback */
void gallery_on_close(void (*closed)(void));
void gallery_close(void);
/* the pictures (indexes into thumbs[]) that are at least partly on screen */
void gallery_visible(int *first, int *last);
int gallery_hover(void);
void gallery_update(void *input);
void gallery_draw(int frame);
