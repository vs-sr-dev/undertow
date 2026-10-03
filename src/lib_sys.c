/* Lua libraries: input, time. */
#include <stdio.h>

#include "input.h"
#include "lauxlib.h"
#include "lualib.h"
#include "runtime.h"
#include "zlibs.h"

static int g_input_mode;

static int push_key(lua_State *L, int key, int remote, uint32_t ts)
{
    lua_pushnumber(L, key);      /* key_id, remote_id, timestamp (sudoku locals) */
    lua_pushnumber(L, remote);
    lua_pushnumber(L, (int)ts);
    if (rt_trace && key != KEY_NONE)
        printf("[%7u]   -> key %d remote %d\n", (unsigned)rt_now_ms(), key, remote);
    return 3;
}

static int in_getkey(lua_State *L)
{
    int key, remote;
    uint32_t ts;
    rt_check_quit(L);
    if (!input_pop(&key, &remote, &ts)) {
        SDL_Delay(1);    /* scripts busy-poll; don't spin a core */
        /* engine's empty event (0x8073550c): key and remote 0xff, time 0. Lock 5 tests
         * remote < NO_KEY to see whether a key came in. */
        return push_key(L, KEY_NONE, KEY_NONE, 0);
    }
    return push_key(L, key, remote, ts);
}

static int in_waitforkey(lua_State *L)
{
    int key, remote;
    uint32_t ts;
    rt_trace_call(L, "input.WaitForKey");
    while (!input_pop(&key, &remote, &ts))
        rt_sleep(L, 5);
    return push_key(L, key, remote, ts);
}

static int in_clearkeyqueue(lua_State *L)
{
    input_clear();
    return 0;
}

static int in_setmode(lua_State *L)
{
    rt_trace_call(L, "input.SetMode");
    g_input_mode = luaL_checkint(L, 1);
    return 0;
}

static int in_getmode(lua_State *L)
{
    lua_pushnumber(L, g_input_mode);
    return 1;
}

static int in_one(lua_State *L)
{
    lua_pushnumber(L, 1);
    return 1;
}

static int in_nop(lua_State *L)
{
    return 0;
}

static const luaL_reg input_lib[] = {
    {"ClearKeyQueue", in_clearkeyqueue}, {"GetKey", in_getkey}, {"WaitForKey", in_waitforkey},
    {"EnableRemotes", in_one}, {"DisableRemotes", in_one}, {"SetMode", in_setmode},
    {"GetMode", in_getmode}, {"SetQueueSize", in_nop}, {"SetRandomKeysTable", in_nop},
    {NULL, NULL}};

static int tm_getrealtime(lua_State *L)
{
    rt_check_quit(L);
    lua_pushnumber(L, (int)rt_now_ms());
    return 1;
}

static int tm_sleep(lua_State *L)
{
    rt_sleep(L, (uint32_t)luaL_optint(L, 1, 0));
    return 0;
}

static const luaL_reg time_lib[] = {
    {"GetRealTime", tm_getrealtime}, {"Sleep", tm_sleep}, {NULL, NULL}};

void libsys_open(lua_State *L)
{
    luaL_openlib(L, "input", input_lib, 0);
    luaL_openlib(L, "time", time_lib, 0);
    lua_settop(L, 0);
}
