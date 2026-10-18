#include <math.h>
#include <stdlib.h>
#include "scroll.h"

#define EDGE_IN 16   /* strip inside the viewport that scrolls */
#define EDGE_OUT 40  /* and how far past the edge it keeps scrolling */
#define MAX_SPEED 26 /* pixels per frame at the far end */

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* slow at the edge, accelerating further out */
static float edge_speed(int depth)
{
    float t = clampf((float)depth / (EDGE_IN + EDGE_OUT), 0, 1);
    return 1.5f + (MAX_SPEED - 1.5f) * t * t;
}

void scroller_update(scroller_t *s, const input_t *in, int x, int y, int w, int h, int max)
{
    if (max < 0) max = 0;
    int over = in->pointer && in->px >= x - 6 && in->px < x + w + 6;

    /* B: a press that moves becomes a drag, a press released in place is a tap */
    s->b_tap = 0;
    if (in->down & BTN_B) {
        if (!(in->held & BTN_B)) {
            s->b_tap = 1; /* a keyboard Esc: no hold to follow */
        } else {
            s->b_held = 1;
            s->b_start = in->py;
            s->b_moved = 0;
            s->b_inside = over && in->py >= y && in->py < y + h; /* only a press on the content can drag it */
        }
    } else if (s->b_held && !(in->held & BTN_B)) {
        s->b_held = 0;
        if (!s->b_moved) s->b_tap = 1;
    }
    if (s->b_held && s->b_inside && !s->b_moved && in->pointer && abs(in->py - s->b_start) > 8) {
        s->b_moved = 1;
        s->dragging = 1;
        s->last_y = in->py;
        s->vel = 0;
        s->target = s->pos;
    }

    if (s->dragging && s->b_held && in->pointer) {
        /* grab: the content follows the pointer one to one */
        float dy = (float)(in->py - s->last_y);
        s->target -= dy;
        s->pos -= dy;
        s->vel = s->vel * 0.5f - dy * 0.5f;
        s->last_y = in->py;
    } else {
        s->dragging = 0;
        if (fabsf(s->vel) > 0.4f) { /* glide after letting go */
            s->target += s->vel;
            s->vel *= 0.93f;
        } else {
            s->vel = 0;
        }
        if (over) {
            int up = y + EDGE_IN - in->py, down = in->py - (y + h - EDGE_IN);
            if (up > 0 && up <= EDGE_IN + EDGE_OUT) s->target -= edge_speed(up);
            if (down > 0 && down <= EDGE_IN + EDGE_OUT) s->target += edge_speed(down);
        }
    }

    s->target = clampf(s->target, 0, (float)max);
    if (s->target <= 0 || s->target >= max) s->vel = 0;
    if (s->dragging) s->pos = clampf(s->pos, 0, (float)max);
    else {
        float d = s->target - s->pos;
        s->pos = fabsf(d) < 0.5f ? s->target : s->pos + d * 0.3f;
    }
}

void scroller_reveal(scroller_t *s, int top, int bottom, int h, int max)
{
    if (top < s->target) s->target = (float)top;
    if (bottom > s->target + h) s->target = (float)(bottom - h);
    s->target = clampf(s->target, 0, (float)(max > 0 ? max : 0));
    s->vel = 0;
}
