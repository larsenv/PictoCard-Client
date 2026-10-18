#pragma once
#include "image.h"
#include "input.h"

/* Doodle pad on the touch screen; the drawing is shown full size on the top screen. */
typedef void (*doodle_cb)(const image_t *drawing);
void doodle_open(doodle_cb cb);
int doodle_active(void);
void doodle_update(const input_t *in);
void doodle_draw_top(void);
void doodle_draw_bottom(void);
