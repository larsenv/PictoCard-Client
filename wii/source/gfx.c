#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include "gfx.h"
#include "image.h"
#include "pointer_png.h"

int SW = 640, SH = 480;
u32 *canvas;

static GXRModeObj *rmode;
static void *xfb[2];
static int which;
static int wide;
static int yoff;
static int clip_x0, clip_y0, clip_x1, clip_y1;
static u16 *src_index; /* widescreen squeeze: source column for each TV column */
static u8 *src_weight;

#define R(c) (((c) >> 16) & 0xFF)
#define G(c) (((c) >> 8) & 0xFF)
#define B(c) ((c) & 0xFF)

void gfx_init(void)
{
    VIDEO_Init();
    rmode = VIDEO_GetPreferredMode(NULL);
    wide = CONF_GetAspectRatio() == CONF_ASPECT_16_9;
    if (wide) { /* use the full width of a 16:9 picture */
        rmode->viWidth = 678;
        rmode->viXOrigin = (VI_MAX_WIDTH_NTSC - rmode->viWidth) / 2;
    }

    SW = wide ? 852 : 640;
    SH = 480;
    canvas = memalign(32, SW * SH * 4);
    memset(canvas, 0, SW * SH * 4);

    src_index = malloc(rmode->fbWidth * sizeof(u16));
    src_weight = malloc(rmode->fbWidth);
    for (int x = 0; x < rmode->fbWidth; x++) {
        float f = (x + 0.5f) * SW / rmode->fbWidth - 0.5f;
        if (f < 0) f = 0;
        int i = (int)f;
        if (i >= SW - 1) {
            i = SW - 2;
            f = SW - 1;
        }
        src_index[x] = i;
        src_weight[x] = (u8)((f - i) * 255);
    }

    for (int i = 0; i < 2; i++) {
        xfb[i] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
        VIDEO_ClearFrameBuffer(rmode, xfb[i], COLOR_BLACK);
    }
    yoff = (rmode->xfbHeight - SH) / 2;
    if (yoff < 0) yoff = 0;

    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(xfb[0]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
    gfx_noclip();
}

int gfx_widescreen(void) { return wide; }

/* ── Cursor ──────────────────────────────────────────────────────────────── */
/* The Wii pointer hand (Player 1) from PrimmR's Wii Pointer Cursors, https://primmr.dev/projects/wii-pointer-cursors/ */
#define CURSOR_HOT_X 10
#define CURSOR_HOT_Y 1
#define UNDER_MAX 64
static u32 under[UNDER_MAX * UNDER_MAX];
static image_t *pointer_img;
static int pointer_tried;

static void cursor_draw(int cx, int cy, int save)
{
    if (!pointer_tried) {
        pointer_tried = 1;
        pointer_img = image_decode(pointer_png, pointer_png_size, 128);
        if (pointer_img && (pointer_img->w > UNDER_MAX || pointer_img->h > UNDER_MAX)) {
            image_free(pointer_img);
            pointer_img = NULL;
        }
    }
    int w = pointer_img ? pointer_img->w : 12, h = pointer_img ? pointer_img->h : 12;
    int x0 = cx - CURSOR_HOT_X, y0 = cy - CURSOR_HOT_Y;
    for (int j = 0; j < h; j++) {
        int y = y0 + j;
        if (y < 0 || y >= SH) continue;
        for (int i = 0; i < w; i++) {
            int x = x0 + i;
            if (x < 0 || x >= SW) continue;
            u32 *p = &canvas[y * SW + x];
            if (save == 1) under[j * UNDER_MAX + i] = *p;
            else if (save == 2) {
                *p = under[j * UNDER_MAX + i];
                continue;
            }
            if (!pointer_img) {
                *p = 0xFFFFFF;
                continue;
            }
            u32 c = pointer_img->px[j * w + i];
            int a = c >> 24;
            if (a) *p = gfx_mix(*p, c & 0xFFFFFF, a);
        }
    }
}

void gfx_present(int cursor, int cx, int cy)
{
    if (cursor) cursor_draw(cx, cy, 1);
#ifdef AUTOTEST
    { extern void autotest_after_draw(void); autotest_after_draw(); } /* screenshots include the pointer */
#endif

    which ^= 1;
    u32 *out = xfb[which];
    int fbw = rmode->fbWidth;
    int pairs = fbw / 2;
    for (int y = 0; y < SH && y + yoff < rmode->xfbHeight; y++) {
        const u32 *row = canvas + y * SW;
        u32 *dst = out + (y + yoff) * pairs;
        for (int x = 0; x < fbw; x += 2) {
            u32 c1, c2;
            if (wide) {
                c1 = gfx_mix(row[src_index[x]], row[src_index[x] + 1], src_weight[x]);
                c2 = gfx_mix(row[src_index[x + 1]], row[src_index[x + 1] + 1], src_weight[x + 1]);
            } else {
                c1 = row[x];
                c2 = row[x + 1];
            }
            int r1 = R(c1), g1 = G(c1), b1 = B(c1);
            int r2 = R(c2), g2 = G(c2), b2 = B(c2);
            int y1 = (16 << 8) + 66 * r1 + 129 * g1 + 25 * b1 + 128;
            int y2 = (16 << 8) + 66 * r2 + 129 * g2 + 25 * b2 + 128;
            int r = r1 + r2, g = g1 + g2, b = b1 + b2;
            int cb = (128 << 9) + -38 * r - 74 * g + 112 * b + 256;
            int cr = (128 << 9) + 112 * r - 94 * g - 18 * b + 256;
            dst[x >> 1] = ((y1 >> 8) << 24) | ((cb >> 9) << 16) | ((y2 >> 8) << 8) | (cr >> 9);
        }
    }
    DCFlushRange(out, rmode->fbWidth * rmode->xfbHeight * 2);
    VIDEO_SetNextFramebuffer(out);
    VIDEO_Flush();

    if (cursor) cursor_draw(cx, cy, 2);
}

/* ── Primitives ──────────────────────────────────────────────────────────── */

/* Clip rectangles nest: each one is intersected with the one around it. */
static int clip_stack[16][4];
static int clip_depth;

void gfx_clip(int x, int y, int w, int h)
{
    if (clip_depth < 16) {
        clip_stack[clip_depth][0] = clip_x0;
        clip_stack[clip_depth][1] = clip_y0;
        clip_stack[clip_depth][2] = clip_x1;
        clip_stack[clip_depth][3] = clip_y1;
        clip_depth++;
    }
    if (x > clip_x0) clip_x0 = x;
    if (y > clip_y0) clip_y0 = y;
    if (x + w < clip_x1) clip_x1 = x + w;
    if (y + h < clip_y1) clip_y1 = y + h;
    if (clip_x1 < clip_x0) clip_x1 = clip_x0;
    if (clip_y1 < clip_y0) clip_y1 = clip_y0;
}

void gfx_noclip(void)
{
    if (clip_depth > 0) {
        clip_depth--;
        clip_x0 = clip_stack[clip_depth][0];
        clip_y0 = clip_stack[clip_depth][1];
        clip_x1 = clip_stack[clip_depth][2];
        clip_y1 = clip_stack[clip_depth][3];
    } else {
        clip_x0 = clip_y0 = 0;
        clip_x1 = SW;
        clip_y1 = SH;
    }
}

u32 gfx_mix(u32 a, u32 b, int alpha)
{
    if (alpha <= 0) return a;
    if (alpha >= 255) return b;
    int ia = 255 - alpha;
    return (((R(a) * ia + R(b) * alpha) / 255) << 16) | (((G(a) * ia + G(b) * alpha) / 255) << 8) |
           ((B(a) * ia + B(b) * alpha) / 255);
}

static inline void plot(int x, int y, u32 color, int alpha)
{
    if (x < clip_x0 || y < clip_y0 || x >= clip_x1 || y >= clip_y1 || alpha <= 0) return;
    u32 *p = &canvas[y * SW + x];
    *p = alpha >= 255 ? color : gfx_mix(*p, color, alpha);
}

void gfx_fill(int x, int y, int w, int h, u32 color)
{
    int x0 = x < clip_x0 ? clip_x0 : x, y0 = y < clip_y0 ? clip_y0 : y;
    int x1 = x + w > clip_x1 ? clip_x1 : x + w, y1 = y + h > clip_y1 ? clip_y1 : y + h;
    for (int j = y0; j < y1; j++) {
        u32 *p = &canvas[j * SW + x0];
        for (int i = x0; i < x1; i++) *p++ = color;
    }
}

void gfx_fill_alpha(int x, int y, int w, int h, u32 color, int alpha)
{
    int x0 = x < clip_x0 ? clip_x0 : x, y0 = y < clip_y0 ? clip_y0 : y;
    int x1 = x + w > clip_x1 ? clip_x1 : x + w, y1 = y + h > clip_y1 ? clip_y1 : y + h;
    for (int j = y0; j < y1; j++) {
        u32 *p = &canvas[j * SW + x0];
        for (int i = x0; i < x1; i++, p++) *p = gfx_mix(*p, color, alpha);
    }
}

static int coverage(float dx, float dy, float r)
{
    float d = sqrtf(dx * dx + dy * dy);
    float c = r - d + 0.5f;
    if (c <= 0) return 0;
    if (c >= 1) return 255;
    return (int)(c * 255);
}

void gfx_round(int x, int y, int w, int h, int r, u32 color)
{
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r <= 0) {
        gfx_fill(x, y, w, h, color);
        return;
    }
    gfx_fill(x, y + r, w, h - 2 * r, color);
    for (int j = 0; j < r; j++) {
        float dy = r - (j + 0.5f);
        gfx_fill(x + r, y + j, w - 2 * r, 1, color);
        gfx_fill(x + r, y + h - 1 - j, w - 2 * r, 1, color);
        for (int i = 0; i < r; i++) {
            int a = coverage(r - (i + 0.5f), dy, r);
            plot(x + i, y + j, color, a);
            plot(x + w - 1 - i, y + j, color, a);
            plot(x + i, y + h - 1 - j, color, a);
            plot(x + w - 1 - i, y + h - 1 - j, color, a);
        }
    }
}

void gfx_frame(int x, int y, int w, int h, int r, u32 fill, u32 border, int t)
{
    gfx_round(x, y, w, h, r, border);
    gfx_round(x + t, y + t, w - 2 * t, h - 2 * t, r > t ? r - t : 0, fill);
}

void gfx_grid(int x, int y, int w, int h, int step, u32 line)
{
    for (int j = 0; j < h; j += step) gfx_fill(x, y + j, w, 1, line);
    for (int i = 0; i < w; i += step) gfx_fill(x + i, y, 1, h, line);
}

void gfx_circle(int cx, int cy, int r, u32 color)
{
    for (int j = -r - 1; j <= r; j++)
        for (int i = -r - 1; i <= r; i++) plot(cx + i, cy + j, color, coverage(i + 0.5f, j + 0.5f, r));
}

void gfx_ring(int cx, int cy, int r, int t, u32 color)
{
    for (int j = -r - 1; j <= r; j++)
        for (int i = -r - 1; i <= r; i++) {
            int outer = coverage(i + 0.5f, j + 0.5f, r);
            int inner = coverage(i + 0.5f, j + 0.5f, r - t);
            plot(cx + i, cy + j, color, outer - inner);
        }
}

void gfx_mask(const u8 *alpha, int w, int h, int x, int y, u32 color)
{
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < clip_y0 || yy >= clip_y1) continue;
        const u8 *a = alpha + j * w;
        u32 *row = canvas + yy * SW;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (!a[i] || xx < clip_x0 || xx >= clip_x1) continue;
            row[xx] = a[i] == 255 ? color : gfx_mix(row[xx], color, a[i]);
        }
    }
}

/* ── Images ──────────────────────────────────────────────────────────────── */

image_t *image_new(int w, int h)
{
    image_t *img = malloc(sizeof(image_t));
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

void gfx_blit_alpha(const image_t *img, int x, int y, int alpha)
{
    if (!img) return;
    for (int j = 0; j < img->h; j++) {
        int yy = y + j;
        if (yy < clip_y0 || yy >= clip_y1) continue;
        const u32 *s = img->px + j * img->w;
        for (int i = 0; i < img->w; i++) {
            int xx = x + i;
            if (xx < clip_x0 || xx >= clip_x1) continue;
            u32 c = s[i];
            int a = ((c >> 24) * alpha) / 255;
            if (a) canvas[yy * SW + xx] = a >= 255 ? (c & 0xFFFFFF) : gfx_mix(canvas[yy * SW + xx], c & 0xFFFFFF, a);
        }
    }
}

void gfx_blit(const image_t *img, int x, int y) { gfx_blit_alpha(img, x, y, 255); }

void gfx_stretch(const image_t *img, int x, int y, int w, int h)
{
    if (!img || w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < clip_y0 || yy >= clip_y1) continue;
        const u32 *s = img->px + (j * img->h / h) * img->w;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx < clip_x0 || xx >= clip_x1) continue;
            u32 c = s[i * img->w / w];
            int a = c >> 24;
            if (a) canvas[yy * SW + xx] = a == 255 ? (c & 0xFFFFFF) : gfx_mix(canvas[yy * SW + xx], c & 0xFFFFFF, a);
        }
    }
}

void gfx_tile(const image_t *img, int x, int y, int w, int h, int tile_w)
{
    if (!img) return;
    for (int i = 0; i < w; i += tile_w) {
        int cw = w - i < tile_w ? w - i : tile_w;
        int ox = clip_x1;
        if (x + i + cw < clip_x1) clip_x1 = x + i + cw;
        gfx_stretch(img, x + i, y, tile_w, h);
        clip_x1 = ox;
    }
}

image_t *image_resize(const image_t *src, int w, int h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    image_t *dst = image_new(w, h);
    if (!dst) return NULL;
    for (int j = 0; j < h; j++) {
        int sy0 = j * src->h / h, sy1 = (j + 1) * src->h / h;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int i = 0; i < w; i++) {
            int sx0 = i * src->w / w, sx1 = (i + 1) * src->w / w;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            u32 a = 0, r = 0, g = 0, b = 0, n = 0;
            for (int y = sy0; y < sy1; y++)
                for (int x = sx0; x < sx1; x++) {
                    u32 c = src->px[y * src->w + x];
                    a += c >> 24;
                    r += (c >> 16) & 0xFF;
                    g += (c >> 8) & 0xFF;
                    b += c & 0xFF;
                    n++;
                }
            dst->px[j * w + i] = ((a / n) << 24) | ((r / n) << 16) | ((g / n) << 8) | (b / n);
        }
    }
    return dst;
}

image_t *image_fit(const image_t *src, int max_w, int max_h)
{
    float s = fminf((float)max_w / src->w, (float)max_h / src->h);
    if (s > 1) s = 1;
    return image_resize(src, (int)(src->w * s + 0.5f), (int)(src->h * s + 0.5f));
}
