#include "audio.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include <SDL.h>

#include "runtime.h"

#define STREAM_FRAMES (AUDIO_RATE * 2)   /* 2 s ring for the movie soundtrack */
#define MAX_SFX 1024
#define MAX_VOICES 16

typedef struct {
    int id;
    int16_t *pcm;      /* mono */
    int nsamples;
} Sfx;

typedef struct {
    int sfx;           /* index into g_sfx, -1 = free */
    int pos;
    int delay;         /* frames of silence before it starts */
} Voice;

static SDL_AudioDeviceID g_dev;
static int16_t g_ring[STREAM_FRAMES * 2];
static int g_rpos, g_fill;               /* in frames */
static Sfx g_sfx[MAX_SFX];
static Voice g_voices[MAX_VOICES];

static int16_t sat16(int v)
{
    return v < -32768 ? -32768 : v > 32767 ? 32767 : (int16_t)v;
}

/* SDL audio thread (device lock held) */
static void mix(void *ud, Uint8 *out8, int len)
{
    int16_t *out = (int16_t *)out8;
    int i, v, frames = len / 4;
    (void)ud;
    for (i = 0; i < frames; i++) {
        int l = 0, r = 0;
        if (g_fill > 0) {
            l = g_ring[g_rpos * 2];
            r = g_ring[g_rpos * 2 + 1];
            g_rpos = (g_rpos + 1) % STREAM_FRAMES;
            g_fill--;
        }
        for (v = 0; v < MAX_VOICES; v++) {
            Voice *vc = &g_voices[v];
            Sfx *s;
            if (vc->sfx < 0)
                continue;
            s = &g_sfx[vc->sfx];
            if (vc->delay > 0) {
                vc->delay--;
                continue;
            }
            if (vc->pos >= s->nsamples) {
                vc->sfx = -1;
                continue;
            }
            l += s->pcm[vc->pos];
            r += s->pcm[vc->pos];
            vc->pos++;
        }
        out[i * 2] = sat16(l);
        out[i * 2 + 1] = sat16(r);
    }
}

int audio_init(void)
{
    SDL_AudioSpec want, have;
    int i;
    for (i = 0; i < MAX_VOICES; i++)
        g_voices[i].sfx = -1;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return -1;
    memset(&want, 0, sizeof(want));
    want.freq = AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = mix;
    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_dev)
        return -1;
    SDL_PauseAudioDevice(g_dev, 0);
    return 0;
}

void audio_shutdown(void)
{
    if (g_dev)
        SDL_CloseAudioDevice(g_dev);
    g_dev = 0;
}

void audio_stream_write(const int16_t *frames, int n, volatile int *abort)
{
    while (n > 0 && g_dev) {
        int chunk, wpos, i;
        SDL_LockAudioDevice(g_dev);
        chunk = STREAM_FRAMES - g_fill;
        if (chunk > n)
            chunk = n;
        wpos = (g_rpos + g_fill) % STREAM_FRAMES;
        for (i = 0; i < chunk; i++) {
            g_ring[wpos * 2] = frames[i * 2];
            g_ring[wpos * 2 + 1] = frames[i * 2 + 1];
            wpos = (wpos + 1) % STREAM_FRAMES;
        }
        g_fill += chunk;
        SDL_UnlockAudioDevice(g_dev);
        frames += chunk * 2;
        n -= chunk;
        if (n > 0) {
            if ((abort && *abort) || rt_quit)
                return;
            SDL_Delay(5);
        }
    }
}

void audio_stream_clear(void)
{
    if (!g_dev)
        return;
    SDL_LockAudioDevice(g_dev);
    g_fill = 0;
    SDL_UnlockAudioDevice(g_dev);
}

int audio_sfx_load(const uint8_t *zwf, size_t size)
{
    uint32_t nsamples;
    uLongf rawlen;
    uint8_t *raw;
    int i, slot = -1;

    if (size < 20)
        return -1;
    nsamples = zwf[4] | (zwf[5] << 8) | (zwf[6] << 16) | ((uint32_t)zwf[7] << 24);
    rawlen = (uLongf)nsamples * 2;
    raw = malloc(rawlen ? rawlen : 1);
    if (!raw || uncompress(raw, &rawlen, zwf + 20, (uLong)(size - 20)) != Z_OK) {
        free(raw);
        return -1;
    }
    for (i = 0; i < MAX_SFX; i++)
        if (g_sfx[i].id == 0) {
            slot = i;
            break;
        }
    if (slot < 0) {
        free(raw);
        return -1;
    }
    nsamples = (uint32_t)(rawlen / 2);
    for (i = 0; i < (int)nsamples; i++) {   /* big-endian -> native, in place */
        int16_t s = (int16_t)((raw[i * 2] << 8) | raw[i * 2 + 1]);
        ((int16_t *)raw)[i] = s;
    }
    SDL_LockAudioDevice(g_dev);
    g_sfx[slot].id = rt_new_id();
    g_sfx[slot].pcm = (int16_t *)raw;
    g_sfx[slot].nsamples = (int)nsamples;
    SDL_UnlockAudioDevice(g_dev);
    return g_sfx[slot].id;
}

static int find_sfx(int id)
{
    int i;
    for (i = 0; i < MAX_SFX; i++)
        if (g_sfx[i].id == id && id)
            return i;
    return -1;
}

void audio_sfx_unload(int id)
{
    int s = find_sfx(id), v;
    if (s < 0)
        return;
    SDL_LockAudioDevice(g_dev);
    for (v = 0; v < MAX_VOICES; v++)
        if (g_voices[v].sfx == s)
            g_voices[v].sfx = -1;
    free(g_sfx[s].pcm);
    memset(&g_sfx[s], 0, sizeof(g_sfx[s]));
    SDL_UnlockAudioDevice(g_dev);
}

void audio_sfx_play(int id, int delay_ms)
{
    int s = find_sfx(id), v;
    if (s < 0)
        return;
    SDL_LockAudioDevice(g_dev);
    for (v = 0; v < MAX_VOICES; v++) {
        if (g_voices[v].sfx < 0) {
            g_voices[v].sfx = s;
            g_voices[v].pos = 0;
            g_voices[v].delay = delay_ms > 0 ? (int)((long long)delay_ms * AUDIO_RATE / 1000) : 0;
            break;
        }
    }
    SDL_UnlockAudioDevice(g_dev);
}

void audio_sfx_stop_all(void)
{
    int v;
    SDL_LockAudioDevice(g_dev);
    for (v = 0; v < MAX_VOICES; v++)
        g_voices[v].sfx = -1;
    SDL_UnlockAudioDevice(g_dev);
}
