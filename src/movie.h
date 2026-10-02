/* Movie player: MPEG-PS (MPEG-2 video + MP2 audio) from the disc, decoded on its own
 * thread, frames shown on the video plane at their timestamps, sound into the mixer.
 * One movie at a time, like the hardware decoder. */
#ifndef UNDERTOW_MOVIE_H
#define UNDERTOW_MOVIE_H

/* Returns 0 on success (Gemz treats any other value as an error); Play starts it. */
int movie_load(const char *disc_path);
void movie_play(void);
void movie_stop(void);
void movie_set_loop(int loop);
int movie_playing(void);
void movie_shutdown(void);

#endif
