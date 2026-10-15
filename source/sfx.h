#pragma once

/* sfx.c — the menu's sound effects (from RiftWii, see the README's credits):
 * a tick when the highlight moves, a click when something is pressed, and a
 * chime when a list, keyboard or gallery opens or closes. */
enum { SFX_HOVER, SFX_CLICK, SFX_OPEN, SFX_CLOSE, SFX_COUNT };

void sfx_init(void);
void sfx_play(int id);
void sfx_shutdown(void);
