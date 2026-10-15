/* GamePad camera: a live preview, then take a photo with A (or a tap) and use
 * it as the card picture. The camera library decodes frames into NV12 surfaces
 * (640x480, a luma plane then interleaved CbCr at half height); two are kept
 * queued and the newest finished one is shown. The preview is mirrored like a
 * mirror, and so is the photo, so it comes out the way it looked. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <camera/camera.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/thread.h>
#include "app.h"
#include "input.h"
#include "font.h"
#include "gfx.h"
#include "ui.h"

#define SURFACES 3
#define VIEW_W 480 /* preview size on screen */
#define VIEW_H 360

enum { CAM_TAKE, CAM_RETAKE, CAM_USE, CAM_CANCEL };

static struct {
    int active, open;
    CAMHandle handle;
    void *work;
    void *surface[SURFACES];
    volatile int ready;      /* index of the newest decoded surface not shown yet, -1 none */
    int shown;               /* the surface on screen, kept out of the queue; -1 none */
    volatile int detached;
    image_t *view;           /* the preview, VIEW_W x VIEW_H */
    image_t *photo;          /* the photo once taken, full size */
    int frames;              /* decoded frames shown so far */
    int focus;
    char error[160];
    camera_cb cb;
    void (*on_error)(const char *message);
} cam;

/* runs on the camera's own thread */
static void on_event(CAMEventData *ev)
{
    if (ev->eventType == CAMERA_DRC_DETACH) {
        cam.detached = 1;
        return;
    }
    if (ev->eventType != CAMERA_DECODE_DONE || ev->decode.failed) return;
    for (int i = 0; i < SURFACES; i++)
        if (cam.surface[i] == ev->decode.surfaceBuffer) cam.ready = i;
}

static int submit(int i)
{
    CAMSurface s = {
        .surfaceSize = CAMERA_YUV_BUFFER_SIZE,
        .surfaceBuffer = cam.surface[i],
        .height = CAMERA_HEIGHT,
        .width = CAMERA_WIDTH,
        .pitch = CAMERA_PITCH,
        .alignment = CAMERA_YUV_BUFFER_ALIGNMENT,
        .tileMode = 0,
        .pixelFormat = 0,
    };
    return CAMSubmitTargetSurface(cam.handle, &s);
}

static void release(void)
{
    if (cam.open) {
        CAMClose(cam.handle);
        CAMExit(cam.handle);
    }
    cam.open = 0;
    for (int i = 0; i < SURFACES; i++) {
        if (cam.surface[i]) MEMFreeToDefaultHeap(cam.surface[i]);
        cam.surface[i] = NULL;
    }
    if (cam.work) MEMFreeToDefaultHeap(cam.work);
    cam.work = NULL;
}

static int start(void)
{
    CAMStreamInfo stream = { .type = CAMERA_STREAM_TYPE_1, .height = CAMERA_HEIGHT, .width = CAMERA_WIDTH };
    int32_t need = CAMGetMemReq(&stream);
    if (need <= 0) return snprintf(cam.error, sizeof cam.error, "The camera isn't available (%d).", (int)need), 0;
    cam.work = MEMAllocFromDefaultHeapEx(need, 256);
    for (int i = 0; i < SURFACES; i++) cam.surface[i] = MEMAllocFromDefaultHeapEx(CAMERA_YUV_BUFFER_SIZE, CAMERA_YUV_BUFFER_ALIGNMENT);
    int surfaces_ok = 1;
    for (int i = 0; i < SURFACES; i++) surfaces_ok &= cam.surface[i] != NULL;
    if (!cam.work || !surfaces_ok) {
        release();
        return snprintf(cam.error, sizeof cam.error, "The Wii U ran out of memory for the camera."), 0;
    }
    CAMSetupInfo setup;
    memset(&setup, 0, sizeof setup);
    setup.streamInfo = stream;
    setup.workMem.size = need;
    setup.workMem.pMem = cam.work;
    setup.eventHandler = on_event;
    setup.mode.forceDrc = FALSE;
    setup.mode.fps = CAMERA_FPS_30;
    setup.threadAffinity = OS_THREAD_ATTRIB_AFFINITY_ANY;
    CAMError err = CAMERA_ERROR_OK;
    cam.handle = CAMInit(0, &setup, &err);
    if (err != CAMERA_ERROR_OK) {
        release();
        if (err == CAMERA_ERROR_DEVICE_IN_USE) snprintf(cam.error, sizeof cam.error, "The camera is in use by something else.");
        else snprintf(cam.error, sizeof cam.error, "Could not start the GamePad camera (error %d).", (int)err);
        return 0;
    }
    err = CAMOpen(cam.handle);
    if (err != CAMERA_ERROR_OK) {
        CAMExit(cam.handle);
        release();
        snprintf(cam.error, sizeof cam.error, "Could not open the GamePad camera (error %d).", (int)err);
        return 0;
    }
    cam.open = 1;
    for (int i = 0; i < SURFACES; i++) submit(i);
    return 1;
}

static inline u32 yuv_rgb(int y, int u, int v)
{
    /* BT.601, video range */
    int c = (y - 16) * 298, d = u - 128, e = v - 128;
    int r = (c + 409 * e + 128) >> 8;
    int g = (c - 100 * d - 208 * e + 128) >> 8;
    int b = (c + 516 * d + 128) >> 8;
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    return 0xFF000000 | r << 16 | g << 8 | b;
}

/* NV12 into an image of any size (nearest sampling), mirrored left to right */
static void convert(const u8 *nv12, image_t *out)
{
    const u8 *uv = nv12 + CAMERA_Y_BUFFER_SIZE;
    for (int j = 0; j < out->h; j++) {
        int sy = j * CAMERA_HEIGHT / out->h;
        const u8 *yrow = nv12 + sy * CAMERA_PITCH, *uvrow = uv + (sy / 2) * CAMERA_PITCH;
        u32 *d = out->px + j * out->w;
        for (int i = 0; i < out->w; i++) {
            int sx = CAMERA_WIDTH - 1 - i * CAMERA_WIDTH / out->w;
            d[i] = yuv_rgb(yrow[sx], uvrow[sx & ~1], uvrow[(sx & ~1) + 1]);
        }
    }
}

void camera_open(camera_cb cb, void (*on_error)(const char *message))
{
    memset(&cam, 0, sizeof cam);
    cam.ready = cam.shown = -1;
    cam.cb = cb;
    cam.on_error = on_error;
    cam.view = image_new(VIEW_W, VIEW_H);
    if (!cam.view || !start()) {
        image_free(cam.view);
        cam.view = NULL;
        if (on_error) on_error(cam.error[0] ? cam.error : "The Wii U ran out of memory for the camera.");
        return;
    }
    for (int i = 0; i < VIEW_W * VIEW_H; i++) cam.view->px[i] = 0xFF000000;
    cam.focus = 1; /* Take photo */
    cam.active = 1;
}

int camera_active(void) { return cam.active; }

static void close_camera(void)
{
    release();
    image_free(cam.view);
    image_free(cam.photo);
    cam.view = cam.photo = NULL;
    cam.active = 0;
}

void camera_shutdown(void)
{
    if (cam.active) close_camera();
}

static void geometry(int *px, int *py, int *pw, int *ph, int *vx, int *vy)
{
    *pw = VIEW_W + 40;
    *ph = VIEW_H + 104;
    *px = (SW - *pw) / 2;
    *py = (SH - *ph) / 2 + 4;
    *vx = *px + 20;
    *vy = *py + 44;
}

/* the buttons under the picture: Cancel and Take photo, or Retake and Use photo */
static void buttons(int px, int py, int pw, int ph, int ids[2], int xs[2], int ws[2], int *by)
{
    *by = py + ph - 44;
    ws[0] = 90;
    ws[1] = 120;
    xs[1] = px + pw - 20 - ws[1];
    xs[0] = xs[1] - 8 - ws[0];
    ids[0] = cam.photo ? CAM_RETAKE : CAM_CANCEL;
    ids[1] = cam.photo ? CAM_USE : CAM_TAKE;
}

static void take(void)
{
    if (cam.shown < 0) return; /* no picture yet */
    image_t *photo = image_new(CAMERA_WIDTH, CAMERA_HEIGHT);
    if (!photo) return;
    convert(cam.surface[cam.shown], photo);
    cam.photo = photo;
    cam.focus = 1;
}

static void press(int id)
{
    switch (id) {
    case CAM_TAKE: take(); break;
    case CAM_RETAKE:
        image_free(cam.photo);
        cam.photo = NULL;
        cam.focus = 1;
        break;
    case CAM_USE: {
        camera_cb cb = cam.cb;
        image_t *photo = cam.photo;
        cam.photo = NULL;
        close_camera();
        if (cb) cb(photo);
        image_free(photo);
        break;
    }
    case CAM_CANCEL: close_camera(); break;
    }
}

void camera_update(void *vin)
{
    input_t *in = vin;
    key_event ev;
    while (input_key(&ev)) {
        if (ev.code == KEY_ESC || ev.code == KEY_BACKSPACE) in->down |= BTN_B;
        else if (ev.code == KEY_ENTER || ev.code == KEY_KP_ENTER || ev.ch == ' ') in->down |= BTN_A;
        else if (ev.code == KEY_LEFT) in->down |= BTN_LEFT;
        else if (ev.code == KEY_RIGHT) in->down |= BTN_RIGHT;
    }
    if (cam.detached) {
        void (*on_error)(const char *) = cam.on_error;
        close_camera();
        if (on_error) on_error("The GamePad camera was disconnected.");
        return;
    }
    /* show the newest frame and hand the one it replaces back to the camera */
    int newest = cam.ready;
    if (!cam.photo && newest >= 0 && newest != cam.shown) {
        cam.ready = -1;
        convert(cam.surface[newest], cam.view);
        if (cam.shown >= 0) submit(cam.shown);
        cam.shown = newest;
        cam.frames++;
    }

    int px, py, pw, ph, vx, vy, ids[2], xs[2], ws[2], by;
    geometry(&px, &py, &pw, &ph, &vx, &vy);
    buttons(px, py, pw, ph, ids, xs, ws, &by);
    if (in->pointer && in->moved)
        for (int b = 0; b < 2; b++)
            if (in->px >= xs[b] && in->px < xs[b] + ws[b] && in->py >= by && in->py < by + 30) cam.focus = b;
    if (in->down & (BTN_LEFT | BTN_RIGHT)) cam.focus = !cam.focus;

    if (in->down & BTN_B) {
        press(cam.photo ? CAM_RETAKE : CAM_CANCEL);
        return;
    }
    if (in->down & BTN_PLUS) {
        press(cam.photo ? CAM_USE : CAM_TAKE);
        return;
    }
    if (in->down & BTN_A) {
        int on_view = in->pointer && in->px >= vx && in->px < vx + VIEW_W && in->py >= vy && in->py < vy + VIEW_H;
        int on_button = -1;
        for (int b = 0; b < 2; b++)
            if (in->pointer && in->px >= xs[b] && in->px < xs[b] + ws[b] && in->py >= by && in->py < by + 30) on_button = b;
        if (on_button >= 0) press(ids[on_button]);
        else if (on_view && !cam.photo) take(); /* tap the picture to take it */
        else if (!in->pointer) press(ids[cam.focus]);
    }
}

void camera_draw(int frame)
{
    (void)frame;
    if (!cam.active) return;
    int px, py, pw, ph, vx, vy, ids[2], xs[2], ws[2], by;
    geometry(&px, &py, &pw, &ph, &vx, &vy);
    ui_dim();
    ui_panel(px, py, pw, ph, NULL, cam.photo ? "Use this photo?" : "GamePad camera");
    gfx_fill(vx - 1, vy - 1, VIEW_W + 2, VIEW_H + 2, T.input_border);
    if (cam.photo) gfx_stretch(cam.photo, vx, vy, VIEW_W, VIEW_H);
    else gfx_blit(cam.view, vx, vy);
    if (!cam.photo && !cam.frames)
        text_draw_center(FONT_REGULAR, 14, vx, vy + VIEW_H / 2 - 10, VIEW_W, 0xFFFFFF, "Starting the camera...");

    buttons(px, py, pw, ph, ids, xs, ws, &by);
    static const char *const LABELS[] = { "Take photo", "Retake", "Use photo", "Cancel" };
    text_draw(FONT_REGULAR, 12, px + 20, by + 8, T.muted,
              cam.photo ? "A use   B retake" : "Tap the picture or press A   B cancel");
    for (int b = 0; b < 2; b++) ui_button(xs[b], by, ws[b], 30, LABELS[ids[b]], b == 1, cam.focus == b, 0);
}
