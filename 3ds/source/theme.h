#pragma once
#include <3ds/types.h>

/* Colours as 0xRRGGBB, after pictocard.net's light and dark themes */
typedef struct {
    u32 bg, panel, elev, line, text, muted, accent, on_accent, danger, ok, select;
} theme_t;

extern const theme_t *T;
void theme_set(int light);
