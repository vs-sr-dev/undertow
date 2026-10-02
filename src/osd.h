/* OSD model: textures, overlays (a texture "frame list" placed at x,y with z-order and
 * visibility) and the video plane underneath. Mutators are called from the game thread and
 * take rt_lock themselves; osd_render runs on the main thread. */
#ifndef UNDERTOW_OSD_H
#define UNDERTOW_OSD_H

#include <stdint.h>

#include <SDL.h>

/* textures */
int osd_texture_add(uint8_t *rgba, int w, int h);   /* takes ownership of rgba */
void osd_texture_free(int tex);
int osd_texture_size(int tex, int *w, int *h);

/* overlays */
int osd_overlay_create(int tex);
void osd_overlay_free(int ovl);
int osd_overlay_add_frame(int ovl, int tex);
int osd_overlay_remove_frame(int ovl, int tex);
int osd_overlay_set_frame(int ovl, int index);
int osd_overlay_set_params(int ovl, int x, int y, int z, int visible);
int osd_overlay_set_pos(int ovl, int x, int y);
int osd_overlay_set_z(int ovl, int z);
int osd_overlay_set_visible(int ovl, int visible);
int osd_overlay_set_alpha(int ovl, int alpha);
int osd_overlay_get(int ovl, int *x, int *y, int *z, int *visible, int *w, int *h);
int osd_overlay_count(void);
int osd_texture_count(void);

/* animations: times are engine ms (time.GetRealTime clock) */
int osd_anim_position(int ovl, int fx, int fy, int tx, int ty, uint32_t start, uint32_t dur);
int osd_anim_alpha(int ovl, uint32_t start, int mode, uint32_t dur);   /* 1 in, 2 out */
int osd_anim_visibility(int ovl, uint32_t start, int visible);
/* steps: nsteps x {cmd, arg, duration}; cmd 1 show frame arg, 2 end, 3 jump to step arg */
int osd_anim_texture(int ovl, uint32_t start, const int *steps, int nsteps);
int osd_anim_count(int ovl);
int osd_anim_clear(int ovl);

/* draw one overlay's current texture into another's (replace: copy pixels, no blending) */
int osd_blit(int src_ovl, int dst_ovl, int x, int y, int replace);

/* scenes: while a scene is open the compositor keeps showing the last complete state */
void osd_begin_scene(void);
void osd_end_scene(void);

/* video plane: a YUV 4:2:0 picture shown under the OSD (iframes, movie frames) */
void osd_video_set(const uint8_t *y, int ystride, const uint8_t *u, const uint8_t *v,
                   int cstride, int w, int h);
void osd_video_clear(void);

/* main thread */
void osd_render(SDL_Renderer *r);

#endif
