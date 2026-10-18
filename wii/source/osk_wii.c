/* The Wii system keyboard look-alike from libwiiosk, in place of the built-in on-screen keyboard.
 * libwiiosk draws with GX while the app draws a software canvas, so this takes the screen over
 * for the length of one edit (as OSK_Prompt does), then puts the app's picture back. Input comes
 * from libwiiinput, so the USB mouse and keyboard work in it too. */
#ifdef HAVE_WIIOSK
#include <malloc.h>
#include <string.h>
#include <gccore.h>
#include <wiiosk.h>
#include <wiiosk_input_wii.h>
#include "gfx.h"
#include "input.h"
#include "app.h"

#define FIFO_SIZE (256 * 1024)

static void *gp_fifo;
static int sound_ready;

static void setup_video(GXRModeObj *rmode)
{
    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
    f32 yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
    u32 xfb_height = GX_SetDispCopyYScale(yscale);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopyDst(rmode->fbWidth, xfb_height);
    GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
    GX_SetFieldMode(rmode->field_rendering, (rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE);
    GX_SetPixelFmt(rmode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_SetCopyClear((GXColor){ 0x20, 0x24, 0x2C, 0xFF }, GX_MAX_Z24);
}


#ifdef AUTOTEST
/* Test builds: type "hello", save the keyboard's picture to sd:/shots/osk.bmp and press OK,
 * since the script cannot reach libwiiosk's own input. */
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
static void autotest_osk(int frame, osk_input *in, GXRModeObj *rmode, const u32 *shown)
{
    if (frame == 20)
        for (const char *c = "hello"; *c; c++) in->text[in->text_count++] = (unsigned char)*c;
    if (frame == 45) {
        mkdir("sd:/shots", 0777);
        FILE *f = fopen("sd:/shots/osk.bmp", "wb");
        if (f) {
            int w = rmode->fbWidth, h = rmode->xfbHeight, row = (w * 3 + 3) & ~3, size = 54 + row * h;
            unsigned char hd[54] = { 'B', 'M' };
#define LE32(o, v) do { hd[o] = (v) & 255; hd[o + 1] = ((v) >> 8) & 255; hd[o + 2] = ((v) >> 16) & 255; hd[o + 3] = ((v) >> 24) & 255; } while (0)
            LE32(2, size); LE32(10, 54); LE32(14, 40); LE32(18, w); LE32(22, h);
            hd[26] = 1; hd[28] = 24; LE32(34, row * h);
            fwrite(hd, 1, 54, f);
            unsigned char *line = calloc(row, 1);
            for (int y = h - 1; y >= 0; y--) {
                for (int x = 0; x < w; x += 2) {
                    u32 px = shown[y * (w / 2) + x / 2];
                    int y0 = (px >> 24) & 255, cb = (px >> 16) & 255, y1 = (px >> 8) & 255, cr = px & 255;
                    for (int k = 0; k < 2; k++) {
                        int yy = (k ? y1 : y0) - 16, d = cb - 128, e = cr - 128;
                        int r = (298 * yy + 409 * e + 128) >> 8, g = (298 * yy - 100 * d - 208 * e + 128) >> 8, b = (298 * yy + 516 * d + 128) >> 8;
                        unsigned char *o = line + (x + k) * 3;
                        o[0] = b < 0 ? 0 : b > 255 ? 255 : b; o[1] = g < 0 ? 0 : g > 255 ? 255 : g; o[2] = r < 0 ? 0 : r > 255 ? 255 : r;
                    }
                }
                fwrite(line, 1, row, f);
            }
            free(line);
            fclose(f);
        }
    }
    if (frame == 70) in->down |= OSK_BTN_CONFIRM;
}
#endif

/* Edits buf in place; returns 1 when the text was accepted, 0 when cancelled. */
int osk_wii_edit(const char *label, char *buf, int buf_size, int max_chars, int multiline, int numeric, int email)
{
    GXRModeObj *rmode = gfx_rmode();
    void *xfb = gfx_xfb_shown();
    u32 fb_size = VIDEO_GetFrameBufferSize(rmode);
    void *saved = malloc(fb_size), *spare = memalign(32, fb_size);
    void *fbs[2];
    if (!saved || !spare) {
        free(saved);
        free(spare);
        return 0;
    }
    memcpy(saved, xfb, fb_size);
    DCInvalidateRange(spare, fb_size);
    fbs[0] = xfb;
    fbs[1] = MEM_K0_TO_K1(spare);

    osk_config cfg;
    OSK_DefaultConfig(&cfg);
    cfg.title = label;
    cfg.initial = buf;
    cfg.max_chars = max_chars > 0 && max_chars <= OSK_MAX_CHARS ? max_chars : OSK_MAX_CHARS;
    cfg.layout = numeric ? OSK_LAYOUT_NUMPAD : OSK_LAYOUT_QWERTY;
    cfg.screen_width = rmode->fbWidth;
    cfg.screen_height = rmode->efbHeight;
    cfg.flags = OSK_FLAG_GX_READY;
    if (gfx_widescreen()) cfg.flags |= OSK_FLAG_WIDESCREEN;
    if (multiline) cfg.flags |= OSK_FLAG_MULTILINE;
    else cfg.flags |= OSK_FLAG_NO_ENTER;
    if (numeric) cfg.flags |= OSK_FLAG_LOCK_LAYOUT;
    osk_keyboard *osk = OSK_Create(&cfg);
    if (!osk) {
        free(saved);
        free(spare);
        return 0;
    }
    if (!sound_ready) {
        OSK_InitSound(false); /* the app's mixer already started ASND */
        sound_ready = 1;
    }

    if (!gp_fifo) {
        gp_fifo = memalign(32, FIFO_SIZE);
        memset(gp_fifo, 0, FIFO_SIZE);
        GX_Init(gp_fifo, FIFO_SIZE);
    }
    setup_video(rmode);
    input_set_canvas(rmode->fbWidth, rmode->efbHeight); /* the pointer now lives in the keyboard's pixels */
    GX_CopyDisp(fbs[1], GX_TRUE);
    GX_DrawDone();

    osk_status st;
    int cur = 1;
    for (int frame = 0;; frame++) {
        INPUT_Update();
        osk_input in;
        OSK_ReadInputWii(&in);
        (void)frame;
#ifdef AUTOTEST
        autotest_osk(frame, &in, rmode, (const u32 *)fbs[cur ^ 1]);
#endif
        st = OSK_Update(osk, &in);

        GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
        OSK_Draw(osk);
        GX_DrawDone();
        GX_SetColorUpdate(GX_TRUE);
        GX_CopyDisp(fbs[cur], GX_TRUE);
        GX_Flush();
        VIDEO_SetNextFramebuffer(fbs[cur]);
        VIDEO_Flush();
        VIDEO_WaitVSync();
        cur ^= 1;
        if (st != OSK_STATUS_EDITING) break;
    }

    int ok = st == OSK_STATUS_DONE;
    if (ok) OSK_CopyText(osk, buf, buf_size);
    OSK_Destroy(osk);

    input_set_canvas(SW, SH);
    memcpy(xfb, saved, fb_size);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    VIDEO_WaitVSync();
    free(saved);
    free(spare);
    return ok;
}
#endif
