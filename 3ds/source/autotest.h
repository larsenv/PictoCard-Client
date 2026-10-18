#pragma once
#include "input.h"

/* Test builds only (make AUTOTEST=1): sdmc:/3ds/PictoCard/autotest.txt drives
 * the app and screenshots of both screens go to sdmc:/3ds/PictoCard/shots/. */
#ifdef AUTOTEST
void autotest_apply(input_t *in);
void autotest_after_draw(void);
/* the text the next keyboard returns instead of opening the system keyboard, or NULL */
const char *autotest_keyboard(void);
extern int autotest_quit;
/* the app sets this: 1 while a network job or a slow step is running */
extern int (*autotest_busy)(void);
#endif
