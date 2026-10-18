#include <string.h>
#include <SDL2/SDL.h>
#include "platform.h"
#include "sfx.h"
#include "sfx_hover_pcm.h"
#include "sfx_click_pcm.h"
#include "sfx_open_pcm.h"
#include "sfx_close_pcm.h"

/* 48 kHz, 16-bit big-endian stereo, as tools/make_sounds.py in RiftWii writes
 * them; big-endian like the Wii U. They are mixed here and SDL plays them on
 * the TV and the GamePad. */
#define VOICES 6

static struct {
    const u8 *pcm;
    u32 len;
    int vol; /* 0..255 */
} sounds[SFX_COUNT] = {
    [SFX_HOVER] = { sfx_hover_pcm, 0, 170 },
    [SFX_CLICK] = { sfx_click_pcm, 0, 220 },
    [SFX_OPEN] = { sfx_open_pcm, 0, 200 },
    [SFX_CLOSE] = { sfx_close_pcm, 0, 200 },
};

typedef struct {
    const s16 *pcm;
    size_t frames, pos;
    int id;
} voice_t;

static voice_t voices[VOICES];
static SDL_AudioDeviceID dev;

static void mix(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    s16 *out = (s16 *)stream;
    int frames = len / 4;
    memset(stream, 0, len);
    for (int v = 0; v < VOICES; v++) {
        voice_t *vc = &voices[v];
        if (!vc->pcm) continue;
        int vol = sounds[vc->id].vol;
        for (int i = 0; i < frames && vc->pos < vc->frames; i++, vc->pos++) {
            for (int ch = 0; ch < 2; ch++) {
                int s = out[i * 2 + ch] + vc->pcm[vc->pos * 2 + ch] * vol / 255;
                out[i * 2 + ch] = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
            }
        }
        if (vc->pos >= vc->frames) vc->pcm = NULL;
    }
}

void sfx_init(void)
{
    sounds[SFX_HOVER].len = sfx_hover_pcm_size;
    sounds[SFX_CLICK].len = sfx_click_pcm_size;
    sounds[SFX_OPEN].len = sfx_open_pcm_size;
    sounds[SFX_CLOSE].len = sfx_close_pcm_size;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 48000;
    want.format = AUDIO_S16MSB;
    want.channels = 2;
    want.samples = 1024;
    want.callback = mix;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev) SDL_PauseAudioDevice(dev, 0);
}

void sfx_play(int id)
{
    if (!dev || id < 0 || id >= SFX_COUNT) return;
    SDL_LockAudioDevice(dev);
    /* a sound that is still playing restarts (ticks don't pile up); others take a free voice */
    int slot = -1;
    for (int v = 0; v < VOICES; v++)
        if (voices[v].pcm && voices[v].id == id) slot = v;
    for (int v = 0; v < VOICES && slot < 0; v++)
        if (!voices[v].pcm) slot = v;
    if (slot >= 0) {
        voices[slot].pcm = (const s16 *)sounds[id].pcm;
        voices[slot].frames = sounds[id].len / 4;
        voices[slot].pos = 0;
        voices[slot].id = id;
    }
    SDL_UnlockAudioDevice(dev);
}

void sfx_shutdown(void)
{
    if (!dev) return;
    SDL_CloseAudioDevice(dev);
    dev = 0;
}
