#pragma once
#include <stddef.h>
#include <3ds/types.h>

/* An RGBA picture, 4 bytes a pixel in R, G, B, A order. */
typedef struct {
    int w, h;
    u8 *px;
} image_t;

image_t *image_new(int w, int h);
void image_free(image_t *img);
/* JPEG, PNG, BMP or GIF; scaled down so neither side is over max_side (0: as is) */
image_t *image_decode(const unsigned char *data, size_t len, int max_side);
/* a copy that fits inside w x h, keeping the aspect ratio (never enlarged) */
image_t *image_fit(const image_t *img, int w, int h);
/* a copy scaled to exactly w x h */
image_t *image_scale(const image_t *img, int w, int h);
unsigned char *image_encode_jpeg(const image_t *img, int quality, size_t *len);
/* why the last decode failed, for the user */
const char *image_error(void);
