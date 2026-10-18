/* Commands, one a line: wait N, press KEY, hold KEY, release KEY, touch X Y,
 * untouch, tap X Y, type TEXT (the next keyboard's answer), shot NAME, idle
 * (until no network job has run for half a second), quit.
 * Keys: A B X Y L R ZL ZR START SELECT UP DOWN LEFT RIGHT. */
#ifdef AUTOTEST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <3ds.h>
#include "autotest.h"
#include "gfx.h"

#define DIR_PATH "sdmc:/3ds/PictoCard"

static FILE *script;
static int wait_frames;
static u32 held_mask, press_mask;
static int touching, tx, ty, tap_frames;
static char typed[2048];
static int has_typed;
int autotest_quit;
int (*autotest_busy)(void);
static int idle_wait, idle_frames;

typedef struct { const char *name; u32 bit; } btn_t;
static const btn_t BTNS[] = {
    { "A", KEY_A }, { "B", KEY_B }, { "X", KEY_X }, { "Y", KEY_Y }, { "L", KEY_L }, { "R", KEY_R },
    { "ZL", KEY_ZL }, { "ZR", KEY_ZR }, { "START", KEY_START }, { "SELECT", KEY_SELECT },
    { "UP", KEY_DUP }, { "DOWN", KEY_DDOWN }, { "LEFT", KEY_DLEFT }, { "RIGHT", KEY_DRIGHT },
};

static u32 key_bit(const char *name)
{
    for (size_t i = 0; i < sizeof BTNS / sizeof *BTNS; i++)
        if (!strcmp(BTNS[i].name, name)) return BTNS[i].bit;
    return 0;
}

/* the framebuffers are 240 pixels tall columns, BGR, bottom-up */
static void save_screen(const char *name, gfxScreen_t screen, int w)
{
    char path[160];
    snprintf(path, sizeof path, DIR_PATH "/shots/%s-%s.bmp", name, screen == GFX_TOP ? "top" : "bottom");
    FILE *f = fopen(path, "wb");
    if (!f) return;
    u16 fw, fh;
    u8 *fb = gfxGetFramebuffer(screen, GFX_LEFT, &fw, &fh);
    int h = 240, row = (w * 3 + 3) & ~3, size = 54 + row * h;
    unsigned char hd[54] = { 'B', 'M' };
#define LE32(o, v) do { hd[o] = (v) & 255; hd[o + 1] = ((v) >> 8) & 255; hd[o + 2] = ((v) >> 16) & 255; hd[o + 3] = ((v) >> 24) & 255; } while (0)
    LE32(2, size); LE32(10, 54); LE32(14, 40); LE32(18, w); LE32(22, h);
    hd[26] = 1; hd[28] = 24; LE32(34, row * h);
    fwrite(hd, 1, 54, f);
    unsigned char *line = calloc(row, 1);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            const u8 *p = fb + ((size_t)x * fw + (fw - 1 - y)) * 3;
            line[x * 3] = p[0];
            line[x * 3 + 1] = p[1];
            line[x * 3 + 2] = p[2];
        }
        fwrite(line, 1, row, f);
    }
    free(line);
    fclose(f);
}

static int pending_shot;
static char shot_name[64];

void autotest_after_draw(void)
{
    if (!pending_shot) return;
    /* the same frame has been drawn twice by now, so either buffer holds it */
    if (--pending_shot) return;
    mkdir(DIR_PATH "/shots", 0777);
    save_screen(shot_name, GFX_TOP, 400);
    save_screen(shot_name, GFX_BOTTOM, 320);
}

const char *autotest_keyboard(void)
{
    if (!has_typed) return NULL;
    has_typed = 0;
    return typed;
}

void autotest_apply(input_t *in)
{
    static int tried;
    if (!script && !tried) {
        tried = 1;
        script = fopen(DIR_PATH "/autotest.txt", "r");
    }
    press_mask = 0;
    if (tap_frames > 0 && --tap_frames == 0) touching = 0;
    if (idle_wait) {
        idle_frames = autotest_busy && autotest_busy() ? 0 : idle_frames + 1;
        if (idle_frames >= 30) idle_wait = 0;
    }
    if (script && wait_frames <= 0 && !pending_shot && !idle_wait) {
        char line[2100];
        while (fgets(line, sizeof line, script)) {
            line[strcspn(line, "\r\n")] = 0;
            char *arg = strchr(line, ' ');
            if (arg) *arg++ = 0;
            if (!strcmp(line, "wait")) {
                wait_frames = arg ? atoi(arg) : 1;
                break;
            } else if (!strcmp(line, "press")) {
                press_mask |= key_bit(arg ? arg : "");
                wait_frames = 2; /* held one frame, released the next */
                break;
            } else if (!strcmp(line, "hold")) {
                held_mask |= key_bit(arg ? arg : "");
            } else if (!strcmp(line, "release")) {
                held_mask &= ~key_bit(arg ? arg : "");
            } else if (!strcmp(line, "touch") && arg) {
                touching = 1;
                sscanf(arg, "%d %d", &tx, &ty);
                wait_frames = 1;
                break;
            } else if (!strcmp(line, "untouch")) {
                touching = 0;
                wait_frames = 1;
                break;
            } else if (!strcmp(line, "tap") && arg) {
                touching = 1;
                tap_frames = 2;
                sscanf(arg, "%d %d", &tx, &ty);
                wait_frames = 3;
                break;
            } else if (!strcmp(line, "type")) {
                snprintf(typed, sizeof typed, "%s", arg ? arg : "");
                for (char *p = typed; *p; p++)
                    if (*p == '|') *p = '\n';
                has_typed = 1;
            } else if (!strcmp(line, "shot") && arg) {
                snprintf(shot_name, sizeof shot_name, "%s", arg);
                pending_shot = 2;
                break;
            } else if (!strcmp(line, "idle")) {
                idle_wait = 1;
                idle_frames = 0;
                break;
            } else if (!strcmp(line, "quit")) {
                autotest_quit = 1;
                break;
            }
        }
    }
    if (wait_frames > 0) wait_frames--;

    static u32 last_held;
    static int was_touching;
    u32 held = held_mask | press_mask;
    in->down |= held & ~last_held;
    in->repeat |= held & ~last_held;
    in->up |= last_held & ~held;
    in->held |= held;
    last_held = held;
    if (touching || was_touching) {
        in->touching = touching;
        in->pressed = touching && !was_touching;
        in->released = !touching && was_touching;
        in->x = tx;
        in->y = ty;
        if (in->pressed) {
            in->start_x = tx;
            in->start_y = ty;
        }
        if (touching) in->held |= KEY_TOUCH;
    }
    was_touching = touching;
}
#endif
