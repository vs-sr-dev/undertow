/* Lua libraries: movie, audio. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "lauxlib.h"
#include "lualib.h"
#include "movie.h"
#include "runtime.h"
#include "vfs.h"
#include "zlibs.h"

/* ---- movie: paths are absolute disc paths (no rm resource) ---- */

static int mv_load(lua_State *L)
{
    rt_trace_call(L, "movie.Load");
    lua_pushnumber(L, movie_load(luaL_checkstring(L, 1)));
    return 1;
}

static int mv_setloop(lua_State *L)
{
    movie_set_loop(luaL_optint(L, 1, 0));
    return 0;
}

static int mv_play(lua_State *L)
{
    rt_trace_call(L, "movie.Play");
    movie_play();
    return 0;
}

static int mv_stop(lua_State *L)
{
    rt_trace_call(L, "movie.Stop");
    movie_stop();
    return 0;
}

static int mv_resume(lua_State *L)
{
    rt_trace_call(L, "movie.Resume");   /* TODO: pause/resume semantics */
    return 0;
}

static int mv_getstate(lua_State *L)
{
    rt_check_quit(L);
    SDL_Delay(1);    /* polled in tight loops */
    lua_pushnumber(L, movie_playing());   /* 0 = finished */
    return 1;
}

static const luaL_reg movie_lib[] = {
    {"Load", mv_load}, {"SetLoop", mv_setloop}, {"Play", mv_play}, {"Stop", mv_stop},
    {"Resume", mv_resume}, {"GetState", mv_getstate}, {NULL, NULL}};

/* ---- audio: sound effects from rm resources ---- */

extern void rm_join_path(char *out, size_t size, int res, const char *name);

static int au_load(lua_State *L)
{
    char path[512];
    size_t size;
    unsigned char *data;
    int id;

    rm_join_path(path, sizeof(path), luaL_checkint(L, 1), luaL_checkstring(L, 2));
    data = vfs_read_all(path, &size);
    id = data ? audio_sfx_load(data, size) : -1;
    free(data);
    if (id < 0)
        fprintf(stderr, "audio.Load: cannot load %s\n", path);
    lua_pushnumber(L, id);
    return 1;
}

static int au_unload(lua_State *L)
{
    audio_sfx_unload(luaL_checkint(L, 1));
    return 0;
}

static int au_play(lua_State *L)
{
    rt_trace_call(L, "audio.Play");
    /* engine 0x8061c9c8 queues {sound, id, now + arg 2}: the 2nd argument delays the
     * start (ms); every known game passes 0 */
    audio_sfx_play(luaL_checkint(L, 1), luaL_optint(L, 2, 0));
    return 0;
}

static const luaL_reg audio_lib[] = {
    {"Load", au_load}, {"Unload", au_unload}, {"Play", au_play}, {NULL, NULL}};

void libmedia_open(lua_State *L)
{
    luaL_openlib(L, "movie", movie_lib, 0);
    luaL_openlib(L, "audio", audio_lib, 0);
    lua_settop(L, 0);
}
