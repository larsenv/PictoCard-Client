/* Picture decoding and encoding with stb_image, plus box-filter scaling. */
#include <stdlib.h>
#include <string.h>
#include "image.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#include "stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb/stb_image_write.h"

/* Big photos are decoded whole before they are scaled, so keep them to what
 * fits next to everything else in an Old 3DS's memory. */
#define MAX_PIXELS (6 * 1000 * 1000)

static const char *last_error = "";

const char *image_error(void) { return last_error; }

image_t *image_new(int w, int h)
{
    if (w <= 0 || h <= 0) return NULL;
    image_t *img = malloc(sizeof *img);
    if (!img) return NULL;
    img->w = w;
    img->h = h;
    img->px = malloc((size_t)w * h * 4);
    if (!img->px) {
        free(img);
        return NULL;
    }
    return img;
}

void image_free(image_t *img)
{
    if (!img) return;
    free(img->px);
    free(img);
}

/* Box filter: every destination pixel averages the source pixels it covers. */
image_t *image_scale(const image_t *src, int w, int h)
{
    image_t *dst = image_new(w, h);
    if (!dst) return NULL;
    for (int y = 0; y < h; y++) {
        int y0 = (int)((long long)y * src->h / h), y1 = (int)((long long)(y + 1) * src->h / h);
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < w; x++) {
            int x0 = (int)((long long)x * src->w / w), x1 = (int)((long long)(x + 1) * src->w / w);
            if (x1 <= x0) x1 = x0 + 1;
            unsigned sum[4] = { 0, 0, 0, 0 }, n = 0;
            for (int sy = y0; sy < y1; sy++) {
                const u8 *p = src->px + ((size_t)sy * src->w + x0) * 4;
                for (int sx = x0; sx < x1; sx++, p += 4) {
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                    sum[3] += p[3];
                    n++;
                }
            }
            u8 *d = dst->px + ((size_t)y * w + x) * 4;
            for (int c = 0; c < 4; c++) d[c] = (u8)(sum[c] / n);
        }
    }
    return dst;
}

image_t *image_fit(const image_t *img, int w, int h)
{
    int dw = img->w, dh = img->h;
    if (dw > w) {
        dh = (int)((long long)dh * w / dw);
        dw = w;
    }
    if (dh > h) {
        dw = (int)((long long)dw * h / dh);
        dh = h;
    }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    if (dw == img->w && dh == img->h) {
        image_t *copy = image_new(dw, dh);
        if (copy) memcpy(copy->px, img->px, (size_t)dw * dh * 4);
        return copy;
    }
    return image_scale(img, dw, dh);
}

image_t *image_decode(const unsigned char *data, size_t len, int max_side)
{
    int w, h, comp;
    if (!stbi_info_from_memory(data, (int)len, &w, &h, &comp)) {
        last_error = "That file is not a picture PictoCard can read. Choose a JPEG, PNG, GIF or BMP.";
        return NULL;
    }
    if ((long long)w * h > MAX_PIXELS) {
        last_error = "That picture is too big for the 3DS (6 megapixels at most).";
        return NULL;
    }
    u8 *px = stbi_load_from_memory(data, (int)len, &w, &h, &comp, 4);
    if (!px) {
        last_error = "That picture could not be read, or the 3DS ran out of memory.";
        return NULL;
    }
    image_t full = { w, h, px };
    image_t *out;
    if (max_side > 0 && (w > max_side || h > max_side)) {
        out = image_fit(&full, max_side, max_side);
    } else {
        out = malloc(sizeof *out);
        if (out) {
            *out = full;
            return out;
        }
    }
    stbi_image_free(px);
    if (!out) last_error = "The 3DS ran out of memory reading that picture.";
    return out;
}

typedef struct {
    unsigned char *data;
    size_t len, cap;
    int failed;
} sink_t;

static void sink_write(void *ctx, void *data, int size)
{
    sink_t *s = ctx;
    if (s->failed) return;
    if (s->len + size > s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 65536;
        while (cap < s->len + size) cap *= 2;
        unsigned char *grown = realloc(s->data, cap);
        if (!grown) {
            s->failed = 1;
            return;
        }
        s->data = grown;
        s->cap = cap;
    }
    memcpy(s->data + s->len, data, size);
    s->len += size;
}

unsigned char *image_encode_jpeg(const image_t *img, int quality, size_t *len)
{
    /* JPEG has no alpha: flatten onto white first */
    size_t n = (size_t)img->w * img->h;
    u8 *rgb = malloc(n * 3);
    if (!rgb) return NULL;
    for (size_t i = 0; i < n; i++) {
        const u8 *p = img->px + i * 4;
        unsigned a = p[3];
        for (int c = 0; c < 3; c++) rgb[i * 3 + c] = (u8)((p[c] * a + 255 * (255 - a)) / 255);
    }
    sink_t s = { 0 };
    int ok = stbi_write_jpg_to_func(sink_write, &s, img->w, img->h, 3, rgb, quality);
    free(rgb);
    if (!ok || s.failed) {
        free(s.data);
        return NULL;
    }
    *len = s.len;
    return s.data;
}
