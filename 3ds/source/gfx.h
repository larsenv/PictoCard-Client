#pragma once
#include <citro2d.h>
#include "image.h"

#define TOP_W 400
#define BOT_W 320
#define SCR_H 240

/* A picture uploaded to the GPU, drawn with citro2d. */
typedef struct {
    C3D_Tex tex;
    Tex3DS_SubTexture sub;
    C2D_Image img;
    int w, h;
    int ok;
} texture_t;

void gfx_init(void);
void gfx_exit(void);
/* frame: begin, then draw to top or bottom, then end */
void gfx_begin(u32 clear_top, u32 clear_bottom);
void gfx_top(void);
void gfx_bottom(void);
void gfx_end(void);

/* (Re)upload img (at most 1024 x 1024); keeps the texture when the size matches */
int tex_set(texture_t *t, const image_t *img);
void tex_free(texture_t *t);
void tex_draw(const texture_t *t, float x, float y, float sx, float sy);

/* 0xRRGGBB and alpha 0..255 */
u32 rgb(u32 hex);
u32 rgba(u32 hex, int alpha);
void rect(float x, float y, float w, float h, u32 color);
void frame_rect(float x, float y, float w, float h, float t, u32 color);

/* Text in the system font. size is in pixels (about the height of a capital
 * plus descender); flags: C2D_AlignLeft/Center/Right. wrap > 0 wraps words. */
void text(float x, float y, float size, u32 color, u32 flags, float wrap, const char *s);
float text_width(float size, const char *s);
float text_height(float size, float wrap, const char *s);
/* like text, but cut with "..." to fit max_w */
void text_fit(float x, float y, float size, u32 color, u32 flags, float max_w, const char *s);
