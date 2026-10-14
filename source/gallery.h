#pragma once
#include "gfx.h"

/* gallery.c — a grid of pictures to pick from (letterheads). The first cell is
 * "none" and reports -1; the others report their index. */
typedef void (*gallery_cb)(int value);
void gallery_open(const char *title, const char *none_label, image_t *const *thumbs, const char *const *names,
                  const char *const *groups, int count, int selected, gallery_cb cb);
int gallery_active(void);
int gallery_hover(void);
void gallery_update(void *input);
void gallery_draw(int frame);
