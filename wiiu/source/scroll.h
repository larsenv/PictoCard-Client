#pragma once
#include "input.h"

/* Smooth scrolling shared by the form, lists and the gallery:
 *  - hold B and move the pointer, or drag a finger on the GamePad, to drag the
 *    content; it keeps gliding when let go
 *  - point at (or just past) the top or bottom edge to scroll, faster further out
 *  - jumps from the D-Pad ease in instead of snapping */
typedef struct {
    float pos, target, vel;
    int dragging, last_y;
    int b_held, b_start, b_moved, b_inside; /* B pressed: a drag once it moves, a tap otherwise */
    int b_tap;                    /* set for one update when B was tapped (Cancel in lists) */
    int t_inside, t_start;        /* a GamePad touch that began on the content, and where */
} scroller_t;

/* x, y, w, h: the scrolling viewport on screen. max: the largest scroll offset. */
void scroller_update(scroller_t *s, const input_t *in, int x, int y, int w, int h, int max);
/* scroll so that [top, bottom) (content coordinates) is visible in a viewport h high */
void scroller_reveal(scroller_t *s, int top, int bottom, int h, int max);
static inline int scroller_px(const scroller_t *s) { return (int)(s->pos + 0.5f); }
