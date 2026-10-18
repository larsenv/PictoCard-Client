#pragma once
#include <3ds.h>

typedef struct {
    u32 down, held, up, repeat;
    int touching;      /* stylus on the screen this frame */
    int pressed;       /* touched down this frame */
    int released;      /* lifted this frame (x, y hold the last position) */
    int x, y;          /* stylus position */
    int start_x, start_y;
    circlePosition cpad;
} input_t;

void input_read(input_t *in);
