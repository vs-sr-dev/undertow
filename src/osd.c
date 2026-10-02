#include "osd.h"

#include <stdlib.h>
#include <string.h>

#include "runtime.h"

#define MAX_FRAMES 64
#define MAX_ANIMS 8
#define MAX_STEPS 64

enum { ANIM_POS, ANIM_ALPHA, ANIM_VIS };

typedef struct {
    int type;
    uint32_t start, dur;
    int a, b, c, d;
} Anim;

typedef struct {
    int cmd, arg, dur;
} TexStep;

typedef struct {
    int id;
    int w, h;
    uint8_t *rgba;
    SDL_Texture *sdl;     /* created lazily by the main thread */
    int dirty;            /* pixels changed (blits): re-upload */
    int dead;             /* freed by the game, released by the main thread */
} Texture;

typedef struct {
    int id;
    int frames[MAX_FRAMES];
    int nframes, active;
    int x, y, z, visible, alpha;
    int seq;              /* creation order, tie-break for equal z */
    Anim anims[MAX_ANIMS];
    int nanims;
    TexStep prog[MAX_STEPS]; /* texture animation program */
    int nprog, pc, prog_on;
    uint32_t prog_t;      /* when the current step starts */
} Overlay;

typedef struct {
    int tex, x, y, alpha, z, seq;
} DrawItem;

static Texture *g_tex;
static int g_ntex, g_captex;
static Overlay *g_ovl;
static int g_novl, g_capovl;
static int g_seq;
static int g_scene_depth;

static DrawItem *g_draw;      /* last complete scene, owned by the main thread */
static int g_ndraw, g_capdraw;

/* video plane */
static uint8_t *g_vy, *g_vu, *g_vv;
static int g_vw, g_vh, g_vserial, g_vshown_serial = -1, g_vvalid;
static SDL_Texture *g_vtex;
static int g_vtex_w, g_vtex_h;

static Texture *find_tex(int id)
{
    int i;
    for (i = 0; i < g_ntex; i++)
        if (g_tex[i].id == id && !g_tex[i].dead)
            return &g_tex[i];
    return NULL;
}

static Overlay *find_ovl(int id)
{
    int i;
    for (i = 0; i < g_novl; i++)
        if (g_ovl[i].id == id)
            return &g_ovl[i];
    return NULL;
}

int osd_texture_add(uint8_t *rgba, int w, int h)
{
    int id = rt_new_id();
    Texture *t;
    SDL_LockMutex(rt_lock);
    if (g_ntex == g_captex) {
        g_captex = g_captex ? g_captex * 2 : 256;
        g_tex = realloc(g_tex, g_captex * sizeof(*g_tex));
    }
    t = &g_tex[g_ntex++];
    memset(t, 0, sizeof(*t));
    t->id = id;
    t->w = w;
    t->h = h;
    t->rgba = rgba;
    SDL_UnlockMutex(rt_lock);
    return id;
}

void osd_texture_free(int id)
{
    Texture *t;
    SDL_LockMutex(rt_lock);
    if ((t = find_tex(id)) != NULL)
        t->dead = 1;
    SDL_UnlockMutex(rt_lock);
}

int osd_texture_size(int id, int *w, int *h)
{
    Texture *t;
    int ok = 0;
    SDL_LockMutex(rt_lock);
    if ((t = find_tex(id)) != NULL) {
        *w = t->w;
        *h = t->h;
        ok = 1;
    }
    SDL_UnlockMutex(rt_lock);
    return ok;
}

int osd_overlay_create(int tex)
{
    int id = rt_new_id();
    Overlay *o;
    SDL_LockMutex(rt_lock);
    if (g_novl == g_capovl) {
        g_capovl = g_capovl ? g_capovl * 2 : 256;
        g_ovl = realloc(g_ovl, g_capovl * sizeof(*g_ovl));
    }
    o = &g_ovl[g_novl++];
    memset(o, 0, sizeof(*o));
    o->id = id;
    o->frames[0] = tex;
    o->nframes = 1;
    o->alpha = 255;
    o->seq = g_seq++;
    SDL_UnlockMutex(rt_lock);
    return id;
}

void osd_overlay_free(int id)
{
    int i;
    SDL_LockMutex(rt_lock);
    for (i = 0; i < g_novl; i++) {
        if (g_ovl[i].id == id) {
            g_ovl[i] = g_ovl[--g_novl];
            break;
        }
    }
    SDL_UnlockMutex(rt_lock);
}

#define WITH_OVL(id, ...)                       \
    do {                                        \
        Overlay *o;                             \
        int ok = 0;                             \
        SDL_LockMutex(rt_lock);                 \
        if ((o = find_ovl(id)) != NULL) {       \
            __VA_ARGS__;                        \
            ok = 1;                             \
        }                                       \
        SDL_UnlockMutex(rt_lock);               \
        return ok;                              \
    } while (0)

int osd_overlay_add_frame(int ovl, int tex)
{
    WITH_OVL(ovl, if (o->nframes < MAX_FRAMES) o->frames[o->nframes++] = tex);
}

int osd_overlay_remove_frame(int ovl, int tex)
{
    WITH_OVL(ovl, {
        int i, j = 0;
        for (i = 0; i < o->nframes; i++)
            if (o->frames[i] != tex)
                o->frames[j++] = o->frames[i];
        o->nframes = j;
        if (o->active >= j)
            o->active = j ? j - 1 : 0;
    });
}

int osd_overlay_set_frame(int ovl, int index)
{
    WITH_OVL(ovl, if (index >= 0 && index < o->nframes) o->active = index);
}

int osd_overlay_set_params(int ovl, int x, int y, int z, int visible)
{
    WITH_OVL(ovl, {
        o->x = x;
        o->y = y;
        o->z = z;
        o->visible = visible;
    });
}

int osd_overlay_set_pos(int ovl, int x, int y)
{
    WITH_OVL(ovl, {
        o->x = x;
        o->y = y;
    });
}

int osd_overlay_set_z(int ovl, int z)
{
    WITH_OVL(ovl, o->z = z);
}

int osd_overlay_set_visible(int ovl, int visible)
{
    WITH_OVL(ovl, o->visible = visible);
}

int osd_overlay_set_alpha(int ovl, int alpha)
{
    WITH_OVL(ovl, o->alpha = alpha < 0 ? 0 : alpha > 255 ? 255 : alpha);
}

int osd_overlay_get(int ovl, int *x, int *y, int *z, int *visible, int *w, int *h)
{
    WITH_OVL(ovl, {
        Texture *t = o->nframes ? find_tex(o->frames[o->active]) : NULL;
        if (x) *x = o->x;
        if (y) *y = o->y;
        if (z) *z = o->z;
        if (visible) *visible = o->visible;
        if (w) *w = t ? t->w : 0;
        if (h) *h = t ? t->h : 0;
    });
}

int osd_overlay_count(void)
{
    return g_novl;
}

int osd_texture_count(void)
{
    return g_ntex;
}

void osd_begin_scene(void)
{
    SDL_LockMutex(rt_lock);
    g_scene_depth++;
    SDL_UnlockMutex(rt_lock);
}

void osd_end_scene(void)
{
    SDL_LockMutex(rt_lock);
    if (g_scene_depth > 0)
        g_scene_depth--;
    SDL_UnlockMutex(rt_lock);
}

/* ---------------- animations (times are engine ms, as from time.GetRealTime) ------- */

static void anim_update_ovl(Overlay *o, uint32_t now)
{
    int i, guard = 0;
    for (i = 0; i < o->nanims;) {
        Anim *a = &o->anims[i];
        int t1000;
        if ((int32_t)(now - a->start) < 0) {
            i++;
            continue;
        }
        t1000 = a->dur ? (int)((uint64_t)(now - a->start) * 1000 / a->dur) : 1000;
        if (t1000 > 1000)
            t1000 = 1000;
        switch (a->type) {
        case ANIM_POS:
            o->x = a->a + (a->c - a->a) * t1000 / 1000;
            o->y = a->b + (a->d - a->b) * t1000 / 1000;
            break;
        case ANIM_ALPHA:   /* mode 1 fade in, 2 fade out */
            o->alpha = a->a == 2 ? 255 - 255 * t1000 / 1000 : 255 * t1000 / 1000;
            break;
        case ANIM_VIS:
            o->visible = a->a;
            break;
        }
        if (t1000 >= 1000)
            o->anims[i] = o->anims[--o->nanims];
        else
            i++;
    }
    while (o->prog_on && (int32_t)(now - o->prog_t) >= 0 && guard++ < MAX_STEPS) {
        TexStep *st = &o->prog[o->pc];
        if (st->cmd == 1) {            /* TA_DISPLAY_TEXTURE frame, duration */
            if (st->arg >= 0 && st->arg < o->nframes)
                o->active = st->arg;
            o->prog_t += st->dur > 0 ? (uint32_t)st->dur : 1;
            o->pc++;
        } else if (st->cmd == 3) {     /* TA_JUMP index */
            o->pc = st->arg;
        } else {                       /* TA_END_ANIMATION */
            o->prog_on = 0;
        }
        if (o->pc < 0 || o->pc >= o->nprog)
            o->prog_on = 0;
    }
}

static void anim_update(void)
{
    uint32_t now = rt_now_ms();
    int i;
    for (i = 0; i < g_novl; i++)
        if (g_ovl[i].nanims || g_ovl[i].prog_on)
            anim_update_ovl(&g_ovl[i], now);
}

static int add_anim(int ovl, Anim a)
{
    WITH_OVL(ovl, {
        if (o->nanims < MAX_ANIMS)
            o->anims[o->nanims++] = a;
    });
}

int osd_anim_position(int ovl, int fx, int fy, int tx, int ty, uint32_t start, uint32_t dur)
{
    Anim a = {ANIM_POS, start, dur, fx, fy, tx, ty};
    return add_anim(ovl, a);
}

int osd_anim_alpha(int ovl, uint32_t start, int mode, uint32_t dur)
{
    Anim a = {ANIM_ALPHA, start, dur, mode, 0, 0, 0};
    return add_anim(ovl, a);
}

int osd_anim_visibility(int ovl, uint32_t start, int visible)
{
    Anim a = {ANIM_VIS, start, 0, visible, 0, 0, 0};
    return add_anim(ovl, a);
}

int osd_anim_texture(int ovl, uint32_t start, const int *steps, int nsteps)
{
    uint32_t now = rt_now_ms();
    WITH_OVL(ovl, {
        int i;
        if (nsteps > MAX_STEPS)
            nsteps = MAX_STEPS;
        for (i = 0; i < nsteps; i++)
            o->prog[i] = (TexStep){steps[i * 3], steps[i * 3 + 1], steps[i * 3 + 2]};
        o->nprog = nsteps;
        o->pc = 0;
        o->prog_t = (int32_t)(start - now) > 0 ? start : now;
        o->prog_on = nsteps > 0;
    });
}

int osd_anim_count(int ovl)
{
    int n = 0;
    SDL_LockMutex(rt_lock);
    anim_update();
    {
        Overlay *o = find_ovl(ovl);
        if (o)
            n = o->nanims + (o->prog_on ? 1 : 0);
    }
    SDL_UnlockMutex(rt_lock);
    return n;
}

int osd_anim_clear(int ovl)
{
    WITH_OVL(ovl, {
        o->nanims = 0;
        o->prog_on = 0;
    });
}

/* Draw src overlay's current texture into dst overlay's current texture at (x,y). */
int osd_blit(int src_ovl, int dst_ovl, int x, int y, int replace)
{
    Overlay *so, *dov;
    Texture *st, *dt;
    int ok = 0, row, col;
    SDL_LockMutex(rt_lock);
    so = find_ovl(src_ovl);
    dov = find_ovl(dst_ovl);
    st = so && so->nframes ? find_tex(so->frames[so->active]) : NULL;
    dt = dov && dov->nframes ? find_tex(dov->frames[dov->active]) : NULL;
    if (st && dt) {
        for (row = 0; row < st->h; row++) {
            int dy = y + row;
            if (dy < 0 || dy >= dt->h)
                continue;
            for (col = 0; col < st->w; col++) {
                int dx = x + col;
                const uint8_t *sp = st->rgba + ((size_t)row * st->w + col) * 4;
                uint8_t *dp;
                int a;
                if (dx < 0 || dx >= dt->w)
                    continue;
                dp = dt->rgba + ((size_t)dy * dt->w + dx) * 4;
                a = sp[3];
                if (replace || dp[3] == 0) {
                    memcpy(dp, sp, 4);
                } else if (a) {   /* source over destination */
                    int da = dp[3] * (255 - a) / 255, oa = a + da, k;
                    for (k = 0; k < 3; k++)
                        dp[k] = (uint8_t)((sp[k] * a + dp[k] * da) / (oa ? oa : 1));
                    dp[3] = (uint8_t)oa;
                }
            }
        }
        dt->dirty = 1;
        ok = 1;
    }
    SDL_UnlockMutex(rt_lock);
    return ok;
}

/* ---------------- video plane ---------------- */

void osd_video_set(const uint8_t *y, int ystride, const uint8_t *u, const uint8_t *v,
                   int cstride, int w, int h)
{
    int row, cw = (w + 1) / 2, ch = (h + 1) / 2;
    SDL_LockMutex(rt_lock);
    if (w != g_vw || h != g_vh) {
        free(g_vy);
        g_vy = malloc((size_t)w * h);
        g_vu = malloc((size_t)cw * ch);
        g_vv = malloc((size_t)cw * ch);
        g_vw = w;
        g_vh = h;
    }
    for (row = 0; row < h; row++)
        memcpy(g_vy + (size_t)row * w, y + (size_t)row * ystride, w);
    for (row = 0; row < ch; row++) {
        memcpy(g_vu + (size_t)row * cw, u + (size_t)row * cstride, cw);
        memcpy(g_vv + (size_t)row * cw, v + (size_t)row * cstride, cw);
    }
    g_vvalid = 1;
    g_vserial++;
    SDL_UnlockMutex(rt_lock);
}

void osd_video_clear(void)
{
    SDL_LockMutex(rt_lock);
    g_vvalid = 0;
    g_vserial++;
    SDL_UnlockMutex(rt_lock);
}

/* ---------------- main thread ---------------- */

static int cmp_draw(const void *a, const void *b)
{
    const DrawItem *x = a, *y = b;
    if (x->z != y->z)
        return x->z < y->z ? -1 : 1;
    return x->seq - y->seq;
}

/* Called with rt_lock held and no scene open: rebuild the draw list and release textures
 * the game freed. */
static void snapshot(void)
{
    int i, j;
    g_ndraw = 0;
    for (i = 0; i < g_novl; i++) {
        Overlay *o = &g_ovl[i];
        if (!o->visible || !o->nframes)
            continue;
        if (g_ndraw == g_capdraw) {
            g_capdraw = g_capdraw ? g_capdraw * 2 : 256;
            g_draw = realloc(g_draw, g_capdraw * sizeof(*g_draw));
        }
        g_draw[g_ndraw].tex = o->frames[o->active];
        g_draw[g_ndraw].x = o->x;
        g_draw[g_ndraw].y = o->y;
        g_draw[g_ndraw].z = o->z;
        g_draw[g_ndraw].alpha = o->alpha;
        g_draw[g_ndraw].seq = o->seq;
        g_ndraw++;
    }
    qsort(g_draw, g_ndraw, sizeof(*g_draw), cmp_draw);
    for (i = j = 0; i < g_ntex; i++) {
        if (g_tex[i].dead) {
            if (g_tex[i].sdl)
                SDL_DestroyTexture(g_tex[i].sdl);
            free(g_tex[i].rgba);
            continue;
        }
        g_tex[j++] = g_tex[i];
    }
    g_ntex = j;
}

static void draw_video(SDL_Renderer *r)
{
    if (g_vserial != g_vshown_serial && g_vvalid) {
        if (!g_vtex || g_vtex_w != g_vw || g_vtex_h != g_vh) {
            if (g_vtex)
                SDL_DestroyTexture(g_vtex);
            g_vtex = SDL_CreateTexture(r, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
                                       g_vw, g_vh);
            g_vtex_w = g_vw;
            g_vtex_h = g_vh;
        }
        SDL_UpdateYUVTexture(g_vtex, NULL, g_vy, g_vw, g_vu, (g_vw + 1) / 2, g_vv,
                             (g_vw + 1) / 2);
    }
    g_vshown_serial = g_vserial;
    if (g_vvalid && g_vtex) {
        SDL_Rect dst = {0, 0, SCREEN_W, SCREEN_H};
        SDL_RenderCopy(r, g_vtex, NULL, &dst);
    }
}

void osd_render(SDL_Renderer *r)
{
    int i;
    SDL_LockMutex(rt_lock);
    anim_update();
    if (g_scene_depth == 0)
        snapshot();
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    draw_video(r);
    for (i = 0; i < g_ndraw; i++) {
        Texture *t = find_tex(g_draw[i].tex);
        SDL_Rect dst;
        if (!t)
            continue;
        if (!t->sdl) {
            t->sdl = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC,
                                       t->w, t->h);
            SDL_UpdateTexture(t->sdl, NULL, t->rgba, t->w * 4);
            SDL_SetTextureBlendMode(t->sdl, SDL_BLENDMODE_BLEND);
        } else if (t->dirty) {
            SDL_UpdateTexture(t->sdl, NULL, t->rgba, t->w * 4);
        }
        t->dirty = 0;
        SDL_SetTextureAlphaMod(t->sdl, (Uint8)g_draw[i].alpha);
        dst.x = g_draw[i].x;
        dst.y = g_draw[i].y;
        dst.w = t->w;
        dst.h = t->h;
        SDL_RenderCopy(r, t->sdl, NULL, &dst);
    }
    SDL_UnlockMutex(rt_lock);
}
