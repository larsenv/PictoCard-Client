#pragma once
#include "image.h"
#include "input.h"

/* Take a picture with the 3DS's outer or inner camera: a live viewfinder on
 * the top screen, then the photo to use or retake. */
typedef void (*camera_cb)(image_t *photo); /* the callback owns the photo */
/* 0 when the camera could not be started (message in *err) */
int camera_open(camera_cb cb, const char **err);
int camera_active(void);
void camera_update(const input_t *in);
void camera_draw_top(void);
void camera_draw_bottom(void);
