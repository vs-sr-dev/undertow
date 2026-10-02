#include "runtime.h"

#include <stdio.h>

#include "lauxlib.h"

SDL_mutex *rt_lock;
volatile int rt_quit;
int rt_trace;

static uint32_t g_start;
static int g_next_id = 1;

void rt_init(void)
{
    rt_lock = SDL_CreateMutex();
    g_start = SDL_GetTicks();
}

uint32_t rt_now_ms(void)
{
    return SDL_GetTicks() - g_start;
}

int rt_new_id(void)
{
    int id;
    SDL_LockMutex(rt_lock);
    id = g_next_id++;
    SDL_UnlockMutex(rt_lock);
    return id;
}

void rt_check_quit(lua_State *L)
{
    if (rt_quit)
        luaL_error(L, "undertow: quit");
}

void rt_sleep(lua_State *L, uint32_t ms)
{
    uint32_t end = SDL_GetTicks() + ms;
    for (;;) {
        uint32_t now = SDL_GetTicks();
        rt_check_quit(L);
        if ((int32_t)(end - now) <= 0)
            break;
        SDL_Delay(end - now > 10 ? 10 : end - now);
    }
}

void rt_trace_call(lua_State *L, const char *name)
{
    char buf[512];
    int i, n = lua_gettop(L);
    size_t len = 0;

    if (!rt_trace)
        return;
    buf[0] = 0;
    for (i = 1; i <= n && len + 40 < sizeof(buf); i++) {
        const char *sep = i > 1 ? ", " : "";
        switch (lua_type(L, i)) {
        case LUA_TNUMBER:
            len += snprintf(buf + len, sizeof(buf) - len, "%s%d", sep, (int)lua_tonumber(L, i));
            break;
        case LUA_TSTRING:
            len += snprintf(buf + len, sizeof(buf) - len, "%s\"%.60s\"", sep, lua_tostring(L, i));
            break;
        case LUA_TNIL:
            len += snprintf(buf + len, sizeof(buf) - len, "%snil", sep);
            break;
        default:
            len += snprintf(buf + len, sizeof(buf) - len, "%s<%s>", sep,
                            lua_typename(L, lua_type(L, i)));
        }
    }
    printf("[%7u] %s(%s)\n", (unsigned)rt_now_ms(), name, buf);
}
