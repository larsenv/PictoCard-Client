#include "input.h"
#include "autotest.h"

void input_read(input_t *in)
{
    static int was_touching, last_x, last_y;
    hidScanInput();
    in->down = hidKeysDown();
    in->held = hidKeysHeld();
    in->up = hidKeysUp();
    in->repeat = hidKeysDownRepeat();
    hidCircleRead(&in->cpad);
    in->touching = (in->held & KEY_TOUCH) != 0;
    in->pressed = in->touching && !was_touching;
    in->released = !in->touching && was_touching;
    if (in->touching) {
        touchPosition t;
        hidTouchRead(&t);
        last_x = t.px;
        last_y = t.py;
    }
    in->x = last_x;
    in->y = last_y;
    if (in->pressed) {
        in->start_x = in->x;
        in->start_y = in->y;
    }
    was_touching = in->touching;
#ifdef AUTOTEST
    autotest_apply(in);
#endif
}
