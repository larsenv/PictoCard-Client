#include <malloc.h>
#include <string.h>
#include <gccore.h>
#include <asndlib.h>
#include "sfx.h"
#include "sfx_hover_pcm.h"
#include "sfx_click_pcm.h"
#include "sfx_open_pcm.h"
#include "sfx_close_pcm.h"

#define HOVER_VOICE 1 /* ticks restart on their own voice instead of piling up */

/* 48 kHz, 16-bit big-endian stereo, as tools/make_sounds.py in RiftWii writes them */
static struct {
    const u8 *src;
    u32 len;
    int vol;
    void *buf; /* ASND wants 32-byte aligned, flushed buffers */
    u32 size;
} sounds[SFX_COUNT] = {
    [SFX_HOVER] = { sfx_hover_pcm, 0, 170 },
    [SFX_CLICK] = { sfx_click_pcm, 0, 220 },
    [SFX_OPEN] = { sfx_open_pcm, 0, 200 },
    [SFX_CLOSE] = { sfx_close_pcm, 0, 200 },
};
static int ready;

void sfx_init(void)
{
    sounds[SFX_HOVER].len = sfx_hover_pcm_size;
    sounds[SFX_CLICK].len = sfx_click_pcm_size;
    sounds[SFX_OPEN].len = sfx_open_pcm_size;
    sounds[SFX_CLOSE].len = sfx_close_pcm_size;
    for (int i = 0; i < SFX_COUNT; i++) {
        u32 size = (sounds[i].len + 31) & ~31u;
        sounds[i].buf = memalign(32, size);
        if (!sounds[i].buf) return;
        memcpy(sounds[i].buf, sounds[i].src, sounds[i].len);
        memset((u8 *)sounds[i].buf + sounds[i].len, 0, size - sounds[i].len);
        DCFlushRange(sounds[i].buf, size);
        sounds[i].size = size;
    }
    ASND_Init();
    ASND_Pause(0);
    ready = 1;
}

void sfx_play(int id)
{
    if (!ready || id < 0 || id >= SFX_COUNT) return;
    int voice = HOVER_VOICE;
    if (id == SFX_HOVER) ASND_StopVoice(voice);
    else {
        voice = ASND_GetFirstUnusedVoice();
        if (voice == HOVER_VOICE) voice = HOVER_VOICE + 1;
        if (voice < 0 || ASND_StatusVoice(voice) != SND_UNUSED) return;
    }
    ASND_SetVoice(voice, VOICE_STEREO_16BIT, 48000, 0, sounds[id].buf, sounds[id].size, sounds[id].vol, sounds[id].vol,
                  NULL);
}

void sfx_shutdown(void)
{
    if (!ready) return;
    ASND_Pause(1);
    ASND_End();
    ready = 0;
}
