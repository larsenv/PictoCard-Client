#pragma once
#include <gccore.h>

/* Software canvas, presented to the TV through the external framebuffer.
 * Colors are 0xRRGGBB; images are 0xAARRGGBB. In 16:9 mode the canvas is
 * wider (852 px) and squeezed back to 640 so nothing looks stretched. */

typedef struct image {
    int w, h;
    u32 *px;
} image_t;

extern int SW, SH;
extern u32 *canvas;

void gfx_init(void);
int gfx_widescreen(void);
GXRModeObj *gfx_rmode(void);
void *gfx_xfb_shown(void);
void gfx_present(int cursor, int cx, int cy);
void gfx_cursor_overlay(void *fb, int cx, int cy); /* pointer hand into a YUY2 frame */

/* gfx_clip pushes a clip rectangle (nested inside the current one); gfx_noclip pops it */
void gfx_clip(int x, int y, int w, int h);
void gfx_noclip(void);

/* alpha 0..255 of b over a. No divisions: this runs for every blended pixel. */
static inline u32 gfx_mix(u32 a, u32 b, int alpha)
{
    if (alpha <= 0) return a;
    if (alpha >= 255) return b;
    u32 al = alpha + (alpha >> 7), ia = 256 - al; /* 0..255 -> 0..256 */
    u32 rb = ((a & 0xFF00FF) * ia + (b & 0xFF00FF) * al) >> 8;
    u32 g = ((a & 0x00FF00) * ia + (b & 0x00FF00) * al) >> 8;
    return (rb & 0xFF00FF) | (g & 0x00FF00);
}
void gfx_fill(int x, int y, int w, int h, u32 color);
void gfx_fill_alpha(int x, int y, int w, int h, u32 color, int alpha);
void gfx_round(int x, int y, int w, int h, int r, u32 color);
void gfx_frame(int x, int y, int w, int h, int r, u32 fill, u32 border, int thickness);
void gfx_grid(int x, int y, int w, int h, int step, u32 line);
void gfx_circle(int cx, int cy, int r, u32 color);
void gfx_ring(int cx, int cy, int r, int thickness, u32 color);

void gfx_mask(const u8 *alpha, int w, int h, int x, int y, u32 color);

void gfx_blit(const image_t *img, int x, int y);
void gfx_blit_opaque(const image_t *img, int x, int y); /* ignores alpha: row copies */
void gfx_blit_alpha(const image_t *img, int x, int y, int alpha);
void gfx_stretch(const image_t *img, int x, int y, int w, int h);
/* repeat an image across a strip, scaled to the strip's height */
void gfx_tile(const image_t *img, int x, int y, int w, int h, int tile_w);

image_t *image_new(int w, int h);
void image_free(image_t *img);
image_t *image_resize(const image_t *src, int w, int h); /* area-averaged */
image_t *image_fit(const image_t *src, int max_w, int max_h);
