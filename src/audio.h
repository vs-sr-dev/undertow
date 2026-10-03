/* Audio output: 44.1 kHz stereo s16 mixer combining the movie soundtrack stream with
 * sound effects (.zwf: zlib, s16 BE mono 44.1 kHz). */
#ifndef UNDERTOW_AUDIO_H
#define UNDERTOW_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#define AUDIO_RATE 44100

int audio_init(void);
void audio_shutdown(void);

/* movie stream: interleaved stereo frames; blocks while the buffer is full unless *abort */
void audio_stream_write(const int16_t *frames, int nframes, volatile int *abort);
void audio_stream_clear(void);

/* sound effects */
int audio_sfx_load(const uint8_t *zwf, size_t size);   /* returns id or -1 */
void audio_sfx_unload(int id);
void audio_sfx_play(int id, int delay_ms);   /* starts after delay_ms */
void audio_sfx_stop_all(void);

#endif
