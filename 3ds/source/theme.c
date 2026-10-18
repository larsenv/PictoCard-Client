#include "theme.h"

static const theme_t DARK = {
    .bg = 0x101318, .panel = 0x181c23, .elev = 0x232933, .line = 0x313a47, .text = 0xf2f4f8,
    .muted = 0x9aa4b2, .accent = 0x4f8cff, .on_accent = 0xffffff, .danger = 0xff6b6b, .ok = 0x3ecf8e,
    .select = 0x24365a,
};
static const theme_t LIGHT = {
    .bg = 0xeef1f6, .panel = 0xffffff, .elev = 0xe6eaf0, .line = 0xd2d8e1, .text = 0x14171c,
    .muted = 0x5d6878, .accent = 0x2f6fe4, .on_accent = 0xffffff, .danger = 0xd43c3c, .ok = 0x1f9d63,
    .select = 0xdbe6fb,
};

const theme_t *T = &DARK;

void theme_set(int light) { T = light ? &LIGHT : &DARK; }
