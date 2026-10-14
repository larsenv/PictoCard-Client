/* Test builds only (make AUTOTEST=1): replays sd:/autotest.txt as controller
 * and keyboard input and saves screenshots to sd:/shots/, so the whole app
 * can be exercised in Dolphin without a person at the controls. */
#ifdef AUTOTEST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "input.h"
#include "gfx.h"

static FILE *script;
static int wait_frames;
static int pointing, px, py;
int autotest_quit;

typedef struct { const char *name; u32 bit; } btn_t;
static const btn_t BTNS[] = {
    { "A", BTN_A }, { "B", BTN_B }, { "UP", BTN_UP }, { "DOWN", BTN_DOWN }, { "LEFT", BTN_LEFT },
    { "RIGHT", BTN_RIGHT }, { "PLUS", BTN_PLUS }, { "MINUS", BTN_MINUS }, { "1", BTN_1 }, { "2", BTN_2 },
    { "HOME", BTN_HOME },
};

void autotest_push_key(u8 code, char ch);

static void save_shot(const char *name)
{
    mkdir("sd:/shots", 0777);
    char path[128];
    snprintf(path, sizeof path, "sd:/shots/%s.bmp", name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int row = (SW * 3 + 3) & ~3, size = 54 + row * SH;
    unsigned char h[54] = { 'B', 'M' };
#define LE32(o, v) do { h[o] = (v) & 255; h[o + 1] = ((v) >> 8) & 255; h[o + 2] = ((v) >> 16) & 255; h[o + 3] = ((v) >> 24) & 255; } while (0)
    LE32(2, size); LE32(10, 54); LE32(14, 40); LE32(18, SW); LE32(22, SH);
    h[26] = 1; h[28] = 24; LE32(34, row * SH);
    fwrite(h, 1, 54, f);
    unsigned char *line = calloc(row, 1);
    for (int y = SH - 1; y >= 0; y--) {
        for (int x = 0; x < SW; x++) {
            u32 c = canvas[y * SW + x];
            line[x * 3] = c & 255;
            line[x * 3 + 1] = (c >> 8) & 255;
            line[x * 3 + 2] = (c >> 16) & 255;
        }
        fwrite(line, 1, row, f);
    }
    free(line);
    fclose(f);
}

static int pending_shot;
static char shot_name[64];

/* called after the frame is drawn */
void autotest_after_draw(void)
{
    if (pending_shot) {
        save_shot(shot_name);
        pending_shot = 0;
    }
}

void autotest_apply(input_t *in)
{
    if (!script) {
        static int tried;
        if (tried) goto pointer;
        tried = 1;
        script = fopen("sd:/autotest.txt", "r");
        if (!script) return;
    }
    if (wait_frames > 0) {
        wait_frames--;
        goto pointer;
    }
    char line[512];
    while (fgets(line, sizeof line, script)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#') continue;
        if (!strncmp(line, "wait ", 5)) {
            wait_frames = atoi(line + 5);
            break;
        } else if (!strncmp(line, "press ", 6)) {
            for (unsigned i = 0; i < sizeof BTNS / sizeof *BTNS; i++)
                if (!strcmp(line + 6, BTNS[i].name)) in->down |= BTNS[i].bit;
            wait_frames = 6;
            break;
        } else if (!strncmp(line, "type ", 5)) {
            for (const char *c = line + 5; *c; c++) autotest_push_key(0x04, *c == '~' ? '\n' : *c);
        } else if (!strncmp(line, "key ", 4)) {
            autotest_push_key((u8)strtol(line + 4, NULL, 16), 0);
        } else if (!strncmp(line, "point ", 6)) {
            pointing = 1;
            sscanf(line + 6, "%d %d", &px, &py);
            in->moved = 1;
            in->pointer = 1;
            in->px = px;
            in->py = py;
            wait_frames = 2;
            return;
        } else if (!strcmp(line, "nopoint")) {
            pointing = 0;
        } else if (!strncmp(line, "shot ", 5)) {
            snprintf(shot_name, sizeof shot_name, "%s", line + 5);
            pending_shot = 1;
            wait_frames = 2;
            break;
        } else if (!strcmp(line, "quit")) {
            autotest_quit = 1;
            break;
        }
    }
pointer:
    /* the script owns the pointer (Dolphin would otherwise feed in the host mouse) */
    in->pointer = pointing;
    if (pointing) {
        in->px = px;
        in->py = py;
    } else {
        in->moved = 0;
    }
}
#endif
