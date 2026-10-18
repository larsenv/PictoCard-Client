#include <string.h>
#include <stdlib.h>
#include <3ds.h>
#include "gfx.h"

#define FONT_PX 30.0f /* the system font's line height at scale 1 */

static C3D_RenderTarget *top_target, *bottom_target;
static C2D_TextBuf text_buf, measure_buf;

void gfx_init(void)
{
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 2);
    C2D_Prepare();
    top_target = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bottom_target = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    text_buf = C2D_TextBufNew(16384);
    measure_buf = C2D_TextBufNew(2048);
}

void gfx_exit(void)
{
    C2D_TextBufDelete(text_buf);
    C2D_TextBufDelete(measure_buf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
}

static u32 clear_bottom_color;

void gfx_begin(u32 clear_top, u32 clear_bottom)
{
    C2D_TextBufClear(text_buf);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(top_target, clear_top);
    clear_bottom_color = clear_bottom;
    C2D_SceneBegin(top_target);
}

void gfx_top(void) { C2D_SceneBegin(top_target); }

void gfx_bottom(void)
{
    C2D_TargetClear(bottom_target, clear_bottom_color);
    C2D_SceneBegin(bottom_target);
}

void gfx_end(void) { C3D_FrameEnd(0); }

static u32 next_pow2(u32 v)
{
    u32 p = 8;
    while (p < v) p <<= 1;
    return p;
}

int tex_set(texture_t *t, const image_t *img)
{
    if (!img || img->w > 1024 || img->h > 1024) return 0;
    u32 pw = next_pow2(img->w), ph = next_pow2(img->h);
    if (t->ok && (t->tex.width != pw || t->tex.height != ph)) tex_free(t);
    if (!t->ok) {
        if (!C3D_TexInit(&t->tex, pw, ph, GPU_RGBA8)) return 0;
        C3D_TexSetFilter(&t->tex, GPU_LINEAR, GPU_LINEAR);
        C3D_TexSetWrap(&t->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        t->ok = 1;
    }
    u8 *dst = t->tex.data;
    memset(dst, 0, pw * ph * 4);
    /* the GPU wants 8x8 tiles in Morton order, each pixel as A, B, G, R */
    for (int y = 0; y < img->h; y++) {
        const u8 *s = img->px + (size_t)y * img->w * 4;
        u32 row = ((y >> 3) * (pw >> 3)) << 6;
        u32 ym = ((y & 1) << 1) | ((y & 2) << 2) | ((y & 4) << 3);
        for (int x = 0; x < img->w; x++, s += 4) {
            u32 i = row + ((x >> 3) << 6) + ((x & 1) | ((x & 2) << 1) | ((x & 4) << 2) | ym);
            u8 *d = dst + i * 4;
            d[0] = s[3];
            d[1] = s[2];
            d[2] = s[1];
            d[3] = s[0];
        }
    }
    C3D_TexFlush(&t->tex);
    t->w = img->w;
    t->h = img->h;
    t->sub.width = img->w;
    t->sub.height = img->h;
    t->sub.left = 0.0f;
    t->sub.top = 1.0f;
    t->sub.right = img->w / (float)pw;
    t->sub.bottom = 1.0f - img->h / (float)ph;
    t->img.tex = &t->tex;
    t->img.subtex = &t->sub;
    return 1;
}

void tex_free(texture_t *t)
{
    if (t->ok) C3D_TexDelete(&t->tex);
    memset(t, 0, sizeof *t);
}

void tex_draw(const texture_t *t, float x, float y, float sx, float sy)
{
    if (!t->ok) return;
    C2D_DrawImageAt(t->img, x, y, 0.5f, NULL, sx, sy);
}

u32 rgb(u32 hex) { return C2D_Color32((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, 0xFF); }
u32 rgba(u32 hex, int alpha) { return C2D_Color32((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, alpha); }

void rect(float x, float y, float w, float h, u32 color) { C2D_DrawRectSolid(x, y, 0.5f, w, h, color); }

void frame_rect(float x, float y, float w, float h, float t, u32 color)
{
    rect(x, y, w, t, color);
    rect(x, y + h - t, w, t, color);
    rect(x, y, t, h, color);
    rect(x + w - t, y, t, h, color);
}

static int parse(C2D_Text *t, const char *s)
{
    if (!s || !*s) return 0;
    if (!C2D_TextParse(t, text_buf, s)) return 0;
    C2D_TextOptimize(t);
    return 1;
}

void text(float x, float y, float size, u32 color, u32 flags, float wrap, const char *s)
{
    C2D_Text t;
    if (!parse(&t, s)) return;
    float sc = size / FONT_PX;
    if (wrap > 0)
        C2D_DrawText(&t, flags | C2D_WithColor | C2D_WordWrap, x, y, 0.5f, sc, sc, color, wrap);
    else
        C2D_DrawText(&t, flags | C2D_WithColor, x, y, 0.5f, sc, sc, color);
}

float text_width(float size, const char *s)
{
    /* measuring needs no glyphs kept for drawing, so it has its own buffer */
    C2D_Text t;
    if (!s || !*s) return 0;
    C2D_TextBufClear(measure_buf);
    if (!C2D_TextParse(&t, measure_buf, s)) return 0;
    float w, h;
    C2D_TextGetDimensions(&t, size / FONT_PX, size / FONT_PX, &w, &h);
    return w;
}

/* Word wrap the same way C2D_WordWrap does, counting lines */
float text_height(float size, float wrap, const char *s)
{
    if (!s || !*s) return size;
    if (wrap <= 0) {
        int lines = 1;
        for (const char *p = s; *p; p++) lines += *p == '\n';
        return lines * size;
    }
    /* C2D_TextGetDimensions ignores wrapping; measure word by word instead */
    int lines = 1;
    float line = 0, space = text_width(size, " ");
    const char *p = s;
    char word[256];
    while (*p) {
        if (*p == '\n') {
            lines++;
            line = 0;
            p++;
            continue;
        }
        if (*p == ' ') {
            line += space;
            p++;
            continue;
        }
        int n = 0;
        while (p[n] && p[n] != ' ' && p[n] != '\n' && n < (int)sizeof word - 1) n++;
        memcpy(word, p, n);
        word[n] = 0;
        float w = text_width(size, word);
        if (line > 0 && line + w > wrap) {
            lines++;
            line = 0;
        }
        line += w;
        p += n;
    }
    return lines * size;
}

void text_fit(float x, float y, float size, u32 color, u32 flags, float max_w, const char *s)
{
    if (!s) return;
    if (text_width(size, s) <= max_w) {
        text(x, y, size, color, flags, 0, s);
        return;
    }
    char buf[512];
    int n = snprintf(buf, sizeof buf - 4, "%s", s);
    if (n > (int)sizeof buf - 5) n = sizeof buf - 5;
    while (n > 0) {
        n--;
        while (n > 0 && (buf[n] & 0xC0) == 0x80) n--; /* stay on a UTF-8 boundary */
        memcpy(buf + n, "...", 4);
        if (text_width(size, buf) <= max_w) break;
    }
    text(x, y, size, color, flags, 0, buf);
}
