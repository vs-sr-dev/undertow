/* Lua libraries: gl (OSD textures/overlays), rm (resource directories), iframe (stills).
 * Semantics that are still guesses are marked TODO; animations are not implemented yet. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lualib.h"
#include "osd.h"
#include "runtime.h"
#include "vfs.h"
#include "video.h"
#include "zbm.h"
#include "zlibs.h"

/* ---------------- rm: resources are disc directories ---------------- */

#define MAX_RES 64

typedef struct {
    int id;
    char path[256];
} Resource;

static Resource g_res[MAX_RES];

static const char *res_path(int id)
{
    int i;
    for (i = 0; i < MAX_RES; i++)
        if (g_res[i].id == id)
            return g_res[i].path;
    return "/";
}

void rm_join_path(char *out, size_t size, int res, const char *name)
{
    const char *dir = res_path(res);
    size_t n = strlen(dir);
    snprintf(out, size, "%s%s%s", dir, n && dir[n - 1] == '/' ? "" : "/", name);
}

static int rm_openresource(lua_State *L)
{
    const char *path = luaL_checkstring(L, 1);
    int i, id = rt_new_id();
    rt_trace_call(L, "rm.OpenResource");
    for (i = 0; i < MAX_RES; i++) {
        if (g_res[i].id == 0) {
            g_res[i].id = id;
            snprintf(g_res[i].path, sizeof(g_res[i].path), "%s", path);
            break;
        }
    }
    lua_pushnumber(L, id);
    return 1;
}

static int rm_closeresource(lua_State *L)
{
    int i, id = luaL_checkint(L, 1);
    for (i = 0; i < MAX_RES; i++)
        if (g_res[i].id == id)
            g_res[i].id = 0;
    return 0;
}

static int rm_loadfile(lua_State *L)
{
    char path[512];
    size_t size;
    unsigned char *data;
    rt_trace_call(L, "rm.LoadFile");
    rm_join_path(path, sizeof(path), luaL_checkint(L, 1), luaL_checkstring(L, 2));
    data = vfs_read_all(path, &size);
    if (!data) {
        fprintf(stderr, "rm.LoadFile: missing %s\n", path);
        lua_pushnil(L);
        lua_pushnumber(L, 0);
        return 2;
    }
    lua_pushlightuserdata(L, data);
    lua_pushnumber(L, (int)size);
    return 2;
}

static int rm_unloadfile(lua_State *L)
{
    if (lua_islightuserdata(L, 2))
        free(lua_touserdata(L, 2));
    return 0;
}

static const luaL_reg rm_lib[] = {
    {"OpenResource", rm_openresource}, {"CloseResource", rm_closeresource},
    {"LoadFile", rm_loadfile}, {"UnloadFile", rm_unloadfile}, {NULL, NULL}};

/* ---------------- gl ---------------- */

static int ret_int(lua_State *L, int v)
{
    lua_pushnumber(L, v);
    return 1;
}

static int gl_selectosdmode(lua_State *L)
{
    rt_trace_call(L, "gl.SelectOSDMode");
    return 0;
}

static int gl_beginscene(lua_State *L)
{
    osd_begin_scene();
    return 0;
}

static int gl_endscene(lua_State *L)
{
    osd_end_scene();
    return 0;
}

static int gl_loadtexture(lua_State *L)
{
    char path[512];
    size_t size;
    unsigned char *data;
    uint8_t *rgba;
    int w, h;

    rm_join_path(path, sizeof(path), luaL_checkint(L, 1), luaL_checkstring(L, 2));
    data = vfs_read_all(path, &size);
    rgba = data ? zbm_decode(data, size, &w, &h) : NULL;
    free(data);
    if (!rgba) {
        fprintf(stderr, "gl.LoadTexture: cannot load %s\n", path);
        return ret_int(L, -1);   /* TODO: engine failure value */
    }
    {
        int id = osd_texture_add(rgba, w, h);
        if (rt_trace)
            printf("[%7u] gl.LoadTexture(%s) -> %d\n", (unsigned)rt_now_ms(), path, id);
        return ret_int(L, id);
    }
}

static int gl_freetexture(lua_State *L)
{
    osd_texture_free(luaL_checkint(L, 1));
    return 0;
}

static int gl_createemptytexture(lua_State *L)
{
    int w = luaL_checkint(L, 1), h = luaL_checkint(L, 2);
    rt_trace_call(L, "gl.CreateEmptyTexture");
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
        return ret_int(L, -1);
    return ret_int(L, osd_texture_add(calloc((size_t)w * h, 4), w, h));
}

static int gl_createoverlay(lua_State *L)
{
    int id = osd_overlay_create(luaL_checkint(L, 1));
    if (rt_trace)
        printf("[%7u] gl.CreateOverlayFromTexture(%d) -> %d\n", (unsigned)rt_now_ms(),
               luaL_checkint(L, 1), id);
    return ret_int(L, id);
}

static int gl_freeoverlay(lua_State *L)
{
    osd_overlay_free(luaL_checkint(L, 1));
    return 0;
}

static int gl_addtexture(lua_State *L)
{
    rt_trace_call(L, "gl.AddTextureToOverlay");
    osd_overlay_add_frame(luaL_checkint(L, 1), luaL_checkint(L, 2));
    return 0;
}

static int gl_removetexture(lua_State *L)
{
    osd_overlay_remove_frame(luaL_checkint(L, 1), luaL_checkint(L, 2));
    return 0;
}

static int gl_setactiveframe(lua_State *L)
{
    rt_trace_call(L, "gl.SetTextureActiveFrame");
    osd_overlay_set_frame(luaL_checkint(L, 1), luaL_checkint(L, 2));   /* TODO: 0/1-based? */
    return 0;
}

static int gl_setparameters(lua_State *L)
{
    osd_overlay_set_params(luaL_checkint(L, 1), luaL_checkint(L, 2), luaL_checkint(L, 3),
                           luaL_checkint(L, 4), luaL_checkint(L, 5));
    return 0;
}

static int gl_setzorder(lua_State *L)
{
    osd_overlay_set_z(luaL_checkint(L, 1), luaL_checkint(L, 2));
    return 0;
}

static int gl_setvisibility(lua_State *L)
{
    osd_overlay_set_visible(luaL_checkint(L, 1), luaL_checkint(L, 2));
    return 0;
}

static int gl_setposition(lua_State *L)
{
    osd_overlay_set_pos(luaL_checkint(L, 1), luaL_checkint(L, 2), luaL_checkint(L, 3));
    return 0;
}

static int gl_getzorder(lua_State *L)
{
    int z = 0;
    osd_overlay_get(luaL_checkint(L, 1), NULL, NULL, &z, NULL, NULL, NULL);
    return ret_int(L, z);
}

static int gl_getvisibility(lua_State *L)
{
    int v = 0;
    osd_overlay_get(luaL_checkint(L, 1), NULL, NULL, NULL, &v, NULL, NULL);
    return ret_int(L, v);
}

static int gl_getposition(lua_State *L)
{
    int x = 0, y = 0;
    osd_overlay_get(luaL_checkint(L, 1), &x, &y, NULL, NULL, NULL, NULL);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    return 2;
}

static int gl_getsize(lua_State *L)
{
    int w = 0, h = 0;
    osd_overlay_get(luaL_checkint(L, 1), NULL, NULL, NULL, NULL, &w, &h);
    lua_pushnumber(L, w);
    lua_pushnumber(L, h);
    return 2;
}

static int gl_settexturealphalevel(lua_State *L)
{
    rt_trace_call(L, "gl.SetTextureAlphaLevel");
    /* TODO: range of the level argument; assume 0..255 on an overlay */
    osd_overlay_set_alpha(luaL_checkint(L, 1), luaL_checkint(L, 2));
    return 0;
}

static int gl_hasanimations(lua_State *L)
{
    rt_check_quit(L);
    SDL_Delay(0);    /* scripts busy-wait on this */
    return ret_int(L, osd_anim_count(luaL_checkint(L, 1)));
}

static int gl_deleteallanimations(lua_State *L)
{
    osd_anim_clear(luaL_checkint(L, 1));
    return 0;
}

/* AddPositionAnimation(ovl, from_x, from_y, to_x, to_y, start_ms, duration_ms) */
static int gl_addpositionanimation(lua_State *L)
{
    rt_trace_call(L, "gl.AddPositionAnimation");
    osd_anim_position(luaL_checkint(L, 1), luaL_checkint(L, 2), luaL_checkint(L, 3),
                      luaL_checkint(L, 4), luaL_checkint(L, 5), (uint32_t)luaL_checkint(L, 6),
                      (uint32_t)luaL_checkint(L, 7));
    return 0;
}

/* AddAlphaAnimation(ovl, start_ms, mode 1=in 2=out, duration_ms) */
static int gl_addalphaanimation(lua_State *L)
{
    rt_trace_call(L, "gl.AddAlphaAnimation");
    osd_anim_alpha(luaL_checkint(L, 1), (uint32_t)luaL_checkint(L, 2), luaL_checkint(L, 3),
                   (uint32_t)luaL_checkint(L, 4));
    return 0;
}

/* AddVisibilityAnimation(ovl, at_ms, visible) */
static int gl_addvisibilityanimation(lua_State *L)
{
    rt_trace_call(L, "gl.AddVisibilityAnimation");
    osd_anim_visibility(luaL_checkint(L, 1), (uint32_t)luaL_checkint(L, 2), luaL_checkint(L, 3));
    return 0;
}

/* CreateTextureAnimation(ovl, start_ms, {{cmd, arg, duration}, ...});
 * cmd 1 TA_DISPLAY_TEXTURE (frame, ms), 2 TA_END_ANIMATION, 3 TA_JUMP (step index) */
static int gl_createtextureanimation(lua_State *L)
{
    int steps[64 * 3], n = 0, i;
    rt_trace_call(L, "gl.CreateTextureAnimation");
    luaL_checktype(L, 3, LUA_TTABLE);
    for (i = 1; n < 64; i++) {
        int k;
        lua_rawgeti(L, 3, i);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            break;
        }
        for (k = 0; k < 3; k++) {
            lua_rawgeti(L, -1, k + 1);
            steps[n * 3 + k] = (int)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        n++;
    }
    osd_anim_texture(luaL_checkint(L, 1), (uint32_t)luaL_optint(L, 2, 0), steps, n);
    return ret_int(L, 0);
}

/* BlitOverlay(src_ovl, dst_ovl, x, y [, ?, blend]): copies pixels by default (Gemz clears
 * board cells by blitting an empty overlay); arg 6 = 1 blends (hypothesis). TODO: arg 5 */
static int gl_blitoverlay(lua_State *L)
{
    osd_blit(luaL_checkint(L, 1), luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4),
             luaL_optint(L, 6, 0) == 0);
    return 0;
}

/* BlitOverlayWithCR(src, dst, x, y [, ?, blend, flag, Y1, Cb1, Cr1, A1, Y2, Cb2, Cr2, A2]):
 * same core as BlitOverlay (0x80613c44) plus a colour replacement (binding 0x80625f60 packs
 * the two colours). Letter Zap passes no colours; replacement is not implemented. */
static int gl_blitoverlaywithcr(lua_State *L)
{
    rt_trace_call(L, "gl.BlitOverlayWithCR");
    if (lua_gettop(L) > 7)
        fprintf(stderr, "gl.BlitOverlayWithCR: colour replacement not implemented\n");
    return gl_blitoverlay(L);
}

/* Not understood yet: traced so they can be studied. */
static int gl_traced_nop(lua_State *L)
{
    rt_trace_call(L, lua_tostring(L, lua_upvalueindex(1)));
    return 0;
}

static int gl_traced_id(lua_State *L)
{
    rt_trace_call(L, lua_tostring(L, lua_upvalueindex(1)));
    return ret_int(L, rt_new_id());
}

static const luaL_reg gl_lib[] = {
    {"SelectOSDMode", gl_selectosdmode}, {"BeginScene", gl_beginscene},
    {"EndScene", gl_endscene}, {"CreateOverlayFromTexture", gl_createoverlay},
    {"FreeOverlay", gl_freeoverlay}, {"LoadTexture", gl_loadtexture},
    {"FreeTexture", gl_freetexture}, {"AddTextureToOverlay", gl_addtexture},
    {"RemoveTextureFromOverlay", gl_removetexture}, {"SetTextureActiveFrame", gl_setactiveframe},
    {"SetParameters", gl_setparameters}, {"SetZorder", gl_setzorder},
    {"SetVisibility", gl_setvisibility}, {"SetPosition", gl_setposition},
    {"GetZorder", gl_getzorder}, {"GetVisibility", gl_getvisibility},
    {"GetPosition", gl_getposition}, {"GetSize", gl_getsize},
    {"HasAnimations", gl_hasanimations}, {"CreateEmptyTexture", gl_createemptytexture},
    {"DeleteAllAnimations", gl_deleteallanimations},
    {"AddPositionAnimation", gl_addpositionanimation},
    {"AddAlphaAnimation", gl_addalphaanimation},
    {"AddVisibilityAnimation", gl_addvisibilityanimation},
    {"CreateTextureAnimation", gl_createtextureanimation}, {"BlitOverlay", gl_blitoverlay},
    {"BlitOverlayWithCR", gl_blitoverlaywithcr},
    {"SetTextureAlphaLevel", gl_settexturealphalevel}, {NULL, NULL}};

static const char *const gl_unknown_nop[] = {
    "SetClipInfo", "ClearOSD", "Show", "AddParabolaAnimation", "AddBlinkingAnimation", NULL};
static const char *const gl_unknown_id[] = {NULL};

static void add_traced(lua_State *L, const char *lib, const char *const *names,
                       lua_CFunction fn)
{
    char full[96];
    lua_getglobal(L, lib);
    for (; *names; names++) {
        snprintf(full, sizeof(full), "%s.%s", lib, *names);
        lua_pushstring(L, *names);
        lua_pushstring(L, full);
        lua_pushcclosure(L, fn, 1);
        lua_settable(L, -3);
    }
    lua_pop(L, 1);
}

/* ---------------- iframe: MPEG stills on the video plane ---------------- */

#define MAX_IFRAMES 64

typedef struct {
    int id;
    YuvImage img;
} Iframe;

static Iframe g_iframes[MAX_IFRAMES];

static int if_load(lua_State *L)
{
    char path[512];
    size_t size;
    unsigned char *data;
    int i;

    rm_join_path(path, sizeof(path), luaL_checkint(L, 1), luaL_checkstring(L, 2));
    data = vfs_read_all(path, &size);
    for (i = 0; i < MAX_IFRAMES && g_iframes[i].id; i++)
        ;
    if (!data || i == MAX_IFRAMES || video_decode_still(data, size, &g_iframes[i].img) != 0) {
        fprintf(stderr, "iframe.Load: cannot load %s\n", path);
        free(data);
        return ret_int(L, -1);
    }
    free(data);
    g_iframes[i].id = rt_new_id();
    return ret_int(L, g_iframes[i].id);
}

static Iframe *find_iframe(int id)
{
    int i;
    for (i = 0; i < MAX_IFRAMES; i++)
        if (g_iframes[i].id == id)
            return &g_iframes[i];
    return NULL;
}

static int if_unload(lua_State *L)
{
    Iframe *f = find_iframe(luaL_checkint(L, 1));
    if (f) {
        video_free_image(&f->img);
        f->id = 0;
    }
    return 0;
}

static int if_show(lua_State *L)
{
    Iframe *f = find_iframe(luaL_checkint(L, 1));
    rt_trace_call(L, "iframe.Show");
    if (f)
        osd_video_set(f->img.y, f->img.w, f->img.u, f->img.v, (f->img.w + 1) / 2, f->img.w,
                      f->img.h);
    return 0;
}

static int if_showpredefined(lua_State *L)
{
    rt_trace_call(L, "iframe.ShowPredefined");   /* TODO: engine built-in stills */
    return 0;
}

static int if_clear(lua_State *L)
{
    rt_trace_call(L, "iframe.Clear");
    osd_video_clear();
    return 0;
}

static const luaL_reg iframe_lib[] = {
    {"Load", if_load}, {"Unload", if_unload}, {"Show", if_show},
    {"ShowPredefined", if_showpredefined}, {"Clear", if_clear}, {NULL, NULL}};

void libgl_open(lua_State *L)
{
    luaL_openlib(L, "rm", rm_lib, 0);
    luaL_openlib(L, "gl", gl_lib, 0);
    luaL_openlib(L, "iframe", iframe_lib, 0);
    lua_settop(L, 0);
    add_traced(L, "gl", gl_unknown_nop, gl_traced_nop);
    add_traced(L, "gl", gl_unknown_id, gl_traced_id);
}
