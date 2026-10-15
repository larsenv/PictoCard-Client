/* The camera captures 640 x 480 RGB565 frames into a buffer; every finished
 * frame is shown at half size on the top screen while the next one comes in.
 * The photo is the last full frame. */
#include <stdlib.h>
#include <string.h>
#include <3ds.h>
#include "camera.h"
#include "gfx.h"
#include "theme.h"

#define CAM_W 640
#define CAM_H 480
#define FRAME_BYTES (CAM_W * CAM_H * 2)
#define VIEW_W 320
#define VIEW_H 240

enum { BTN_SHOOT, BTN_SWITCH, BTN_CANCEL, BTN_COUNT };

static struct {
    int active;
    int inner;        /* the inner (selfie) camera */
    int shot;         /* showing the photo just taken */
    int running;
    int receiving;
    int have_frame;
    u32 transfer;
    Handle recv_event, error_event;
    u16 *buf;         /* the frame being received */
    u16 *frame;       /* the last whole frame */
    texture_t tex;
    int dirty;
    camera_cb cb;
} cam;

static u32 select_cam(void) { return cam.inner ? SELECT_IN1 : SELECT_OUT1; }

static void stop(void)
{
    if (!cam.running) return;
    CAMU_StopCapture(PORT_CAM1);
    if (cam.recv_event) svcCloseHandle(cam.recv_event);
    cam.recv_event = 0;
    cam.receiving = 0;
    CAMU_Activate(SELECT_NONE);
    cam.running = 0;
}

static int start(void)
{
    u32 sel = select_cam();
    if (R_FAILED(CAMU_SetSize(sel, SIZE_VGA, CONTEXT_A)) ||
        R_FAILED(CAMU_SetOutputFormat(sel, OUTPUT_RGB_565, CONTEXT_A)))
        return 0;
    CAMU_SetFrameRate(sel, FRAME_RATE_30);
    CAMU_SetNoiseFilter(sel, true);
    CAMU_SetAutoExposure(sel, true);
    CAMU_SetAutoWhiteBalance(sel, true);
    CAMU_SetTrimming(PORT_CAM1, false);
    CAMU_GetMaxBytes(&cam.transfer, CAM_W, CAM_H);
    CAMU_SetTransferBytes(PORT_CAM1, cam.transfer, CAM_W, CAM_H);
    if (R_FAILED(CAMU_Activate(sel))) return 0;
    if (!cam.error_event) CAMU_GetBufferErrorInterruptEvent(&cam.error_event, PORT_CAM1);
    CAMU_ClearBuffer(PORT_CAM1);
    if (R_FAILED(CAMU_StartCapture(PORT_CAM1))) {
        CAMU_Activate(SELECT_NONE);
        return 0;
    }
    cam.running = 1;
    cam.receiving = 0;
    return 1;
}

static void close_camera(void)
{
    stop();
    if (cam.error_event) svcCloseHandle(cam.error_event);
    cam.error_event = 0;
    camExit();
    free(cam.buf);
    free(cam.frame);
    cam.buf = cam.frame = NULL;
    tex_free(&cam.tex);
    cam.active = 0;
}

int camera_open(camera_cb cb, const char **err)
{
    memset(&cam, 0, sizeof cam);
    cam.buf = malloc(FRAME_BYTES);
    cam.frame = malloc(FRAME_BYTES);
    if (!cam.buf || !cam.frame) {
        free(cam.buf);
        free(cam.frame);
        *err = "The 3DS ran out of memory for the camera.";
        return 0;
    }
    if (R_FAILED(camInit())) {
        free(cam.buf);
        free(cam.frame);
        *err = "Could not start the camera.";
        return 0;
    }
    if (!start()) {
        camExit();
        free(cam.buf);
        free(cam.frame);
        *err = "Could not start the camera.";
        return 0;
    }
    cam.cb = cb;
    cam.active = 1;
    return 1;
}

int camera_active(void) { return cam.active; }

/* Collect a finished frame, if there is one, and ask for the next */
static void poll_frames(void)
{
    if (!cam.running) return;
    if (cam.error_event && svcWaitSynchronization(cam.error_event, 0) == 0) {
        /* the camera dropped data (it happens while the system is busy): start over */
        stop();
        start();
        return;
    }
    if (cam.receiving && svcWaitSynchronization(cam.recv_event, 0) == 0) {
        svcCloseHandle(cam.recv_event);
        cam.recv_event = 0;
        cam.receiving = 0;
        u16 *done = cam.buf;
        cam.buf = cam.frame;
        cam.frame = done;
        cam.have_frame = 1;
        cam.dirty = 1;
    }
    if (!cam.receiving) {
        if (R_SUCCEEDED(CAMU_SetReceiving(&cam.recv_event, cam.buf, PORT_CAM1, FRAME_BYTES, (s16)cam.transfer)))
            cam.receiving = 1;
    }
}

/* Half size into an RGB565 texture; the inner camera's view is mirrored, like a mirror */
static void upload_view(void)
{
    if (!cam.dirty || !cam.have_frame) return;
    if (!cam.tex.ok) {
        if (!C3D_TexInit(&cam.tex.tex, 512, 256, GPU_RGB565)) return;
        C3D_TexSetFilter(&cam.tex.tex, GPU_LINEAR, GPU_LINEAR);
        memset(cam.tex.tex.data, 0, 512 * 256 * 2);
        cam.tex.ok = 1;
        cam.tex.w = VIEW_W;
        cam.tex.h = VIEW_H;
        cam.tex.sub = (Tex3DS_SubTexture){ VIEW_W, VIEW_H, 0.0f, 1.0f, VIEW_W / 512.0f, 1.0f - VIEW_H / 256.0f };
        cam.tex.img.tex = &cam.tex.tex;
        cam.tex.img.subtex = &cam.tex.sub;
    }
    int mirror = cam.inner && !cam.shot;
    u16 *dst = cam.tex.tex.data;
    for (int y = 0; y < VIEW_H; y++) {
        const u16 *src = cam.frame + (size_t)(y * 2) * CAM_W;
        u32 row = ((y >> 3) * (512 >> 3)) << 6;
        u32 ym = ((y & 1) << 1) | ((y & 2) << 2) | ((y & 4) << 3);
        for (int x = 0; x < VIEW_W; x++) {
            int sx = mirror ? (VIEW_W - 1 - x) * 2 : x * 2;
            dst[row + ((x >> 3) << 6) + ((x & 1) | ((x & 2) << 1) | ((x & 4) << 2) | ym)] = src[sx];
        }
    }
    C3D_TexFlush(&cam.tex.tex);
    cam.dirty = 0;
}

static image_t *photo(void)
{
    image_t *img = image_new(CAM_W, CAM_H);
    if (!img) return NULL;
    for (int i = 0; i < CAM_W * CAM_H; i++) {
        u16 c = cam.frame[i];
        u8 *p = img->px + (size_t)i * 4;
        p[0] = ((c >> 11) & 0x1F) * 255 / 31;
        p[1] = ((c >> 5) & 0x3F) * 255 / 63;
        p[2] = (c & 0x1F) * 255 / 31;
        p[3] = 0xFF;
    }
    return img;
}

static void shoot(void)
{
    if (!cam.have_frame) return;
    CAMU_PlayShutterSound(SHUTTER_SOUND_TYPE_NORMAL);
    stop(); /* keep the last frame on screen */
    cam.shot = 1;
    cam.dirty = 1;
}

static void retake(void)
{
    cam.shot = 0;
    start();
}

static void use_photo(void)
{
    image_t *img = photo();
    camera_cb cb = cam.cb;
    close_camera();
    if (img && cb) cb(img);
    else image_free(img);
}

static void switch_camera(void)
{
    stop();
    cam.inner = !cam.inner;
    cam.have_frame = 0;
    start();
}

static void button(int b)
{
    if (cam.shot) {
        if (b == BTN_SHOOT) use_photo();
        else if (b == BTN_SWITCH) retake();
        else close_camera();
        return;
    }
    if (b == BTN_SHOOT) shoot();
    else if (b == BTN_SWITCH) switch_camera();
    else close_camera();
}

void camera_update(const input_t *in)
{
    if (!cam.active) return;
    poll_frames();
    if (cam.shot) {
        if (in->down & (KEY_A | KEY_START)) use_photo();
        else if (in->down & KEY_B) retake();
    } else {
        if (in->down & (KEY_A | KEY_L | KEY_R)) shoot();
        else if (in->down & KEY_X) switch_camera();
        else if (in->down & KEY_B) close_camera();
    }
    if (!cam.active) return;
    if (in->released && in->start_y >= 150 && in->y >= 150) {
        int b = in->x * BTN_COUNT / BOT_W;
        if (b >= 0 && b < BTN_COUNT) button(b);
    }
}

void camera_draw_top(void)
{
    rect(0, 0, TOP_W, SCR_H, rgb(0x000000));
    upload_view();
    if (cam.have_frame && cam.tex.ok) {
        tex_draw(&cam.tex, (TOP_W - VIEW_W) / 2.0f, 0, 1, 1);
    } else {
        text(TOP_W / 2, 110, 14, rgb(0xcccccc), C2D_AlignCenter, 0, "Starting the camera...");
    }
    if (cam.shot) {
        rect(0, SCR_H - 22, TOP_W, 22, rgba(0x000000, 170));
        text(TOP_W / 2, SCR_H - 19, 13, rgb(0xffffff), C2D_AlignCenter, 0, "A use this picture   B retake");
    }
}

void camera_draw_bottom(void)
{
    rect(0, 0, BOT_W, SCR_H, rgb(T->panel));
    text(12, 10, 16, rgb(T->text), C2D_AlignLeft, 0, cam.shot ? "Use this picture?" : "Take a picture");
    text(12, 36, 12, rgb(T->muted), C2D_AlignLeft, BOT_W - 24,
         cam.shot ? "It goes on your card or letter. Retake it if it isn't right."
                  : cam.inner ? "Inner camera. A, L or R takes the picture; X switches to the outer camera."
                              : "Outer camera. A, L or R takes the picture; X switches to the inner camera.");
    static const char *LIVE[BTN_COUNT] = { "Take", "Switch", "Cancel" };
    static const char *SHOT[BTN_COUNT] = { "Use it", "Retake", "Cancel" };
    float bw = BOT_W / (float)BTN_COUNT;
    for (int b = 0; b < BTN_COUNT; b++) {
        int main = b == BTN_SHOOT;
        rect(b * bw + 4, 154, bw - 8, 78, rgb(main ? T->accent : T->elev));
        text(b * bw + bw / 2, 182, 16, rgb(main ? T->on_accent : T->text), C2D_AlignCenter, 0,
             cam.shot ? SHOT[b] : LIVE[b]);
    }
}
