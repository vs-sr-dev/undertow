/* ZIT engine Lua libraries: registration, simple libraries (bit, zmath, log, toint) and
 * tracing stubs for libraries not implemented yet. Stubs log each call and return plausible
 * values so scripts keep running. Function lists mirror the engine's luaL_reg tables
 * (docs/engine_api_tables.txt). Real implementations live in lib_*.c. */
#include "zlibs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lualib.h"
#include "runtime.h"

static unsigned g_rand = 12345;

/* ---- tracing stubs ---------------------------------------------------------------- */

enum { R_NONE, R_ID, R_ZERO, R_ONE, R_POS, R_NILZERO, R_MEM, R_VERSION, R_STRS };

typedef struct {
    const char *name;
    int ret;
} StubFn;

static int stub_call(lua_State *L)
{
    const char *name = lua_tostring(L, lua_upvalueindex(1));
    int ret = (int)lua_tonumber(L, lua_upvalueindex(2));

    rt_check_quit(L);
    rt_trace_call(L, name);
    switch (ret) {
    case R_ID: lua_pushnumber(L, rt_new_id()); return 1;
    case R_ZERO: lua_pushnumber(L, 0); return 1;
    case R_ONE: lua_pushnumber(L, 1); return 1;
    case R_POS: lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 2;
    case R_NILZERO: lua_pushnil(L); lua_pushnumber(L, 0); return 2;
    case R_MEM: lua_pushnumber(L, 8 << 20); return 1;
    case R_VERSION: lua_pushstring(L, "0.11.3.undertow"); return 1;
    case R_STRS: lua_pushstring(L, ""); lua_pushstring(L, ""); lua_pushnumber(L, 0); return 3;
    }
    return 0;
}

static void open_stubs(lua_State *L, const char *lib, const StubFn *fns)
{
    char full[96];
    lua_pushstring(L, lib);
    lua_newtable(L);
    for (; fns->name; fns++) {
        snprintf(full, sizeof(full), "%s.%s", lib, fns->name);
        lua_pushstring(L, fns->name);
        lua_pushstring(L, full);
        lua_pushnumber(L, fns->ret);
        lua_pushcclosure(L, stub_call, 2);
        lua_settable(L, -3);
    }
    lua_settable(L, LUA_GLOBALSINDEX);
}

static const StubFn engine_fns[] = {
    {"ZMM_SetLeakDebugMode", R_NONE}, {"ZMM_SetCheckPoint", R_NONE},
    {"ZMM_VerifyCheckPoint", R_NONE}, {"ZMM_GetTotalAllocMemory", R_MEM},
    {"ZMM_GetTotalFreeMemory", R_MEM}, {"ZMM_GetMaxFreeMemory", R_MEM}, {"DR_SetEnable", R_NONE},
    {"DR_SetDebug", R_NONE}, {"DR_SetAbsoluteCapping", R_NONE}, {"DR_ClearPairCappings", R_NONE},
    {"DR_AddPairCapping", R_NONE}, {"OpenTray", R_NONE}, {"CloseTray", R_NONE},
    {"EjectTray", R_NONE}, {"GetTrayState", R_ZERO}, {"SoftwareReset", R_NONE},
    {"WaitForDisk", R_NONE}, {"GetNumOverlays", R_ZERO}, {"GetNumTextures", R_ZERO},
    {"Version", R_VERSION}, {"SetLineTraceMode", R_NONE}, {"GetLineTraceMode", R_ZERO},
    {NULL, 0}};
static const StubFn spi_fns[] = {
    {"Init", R_ZERO}, {"Open", R_ZERO}, {"Close", R_ZERO}, {"Write", R_ZERO}, {"Read", R_ZERO},
    {NULL, 0}};
static const StubFn exp_int_fns[] = {
    {"Configure", R_ZERO}, {"Close", R_ZERO}, {"Test", R_ZERO}, {NULL, 0}};
static const StubFn uart_fns[] = {
    {"RxD", R_ZERO}, {"TxD", R_ZERO}, {"Open", R_ONE}, {"Close", R_ONE}, {NULL, 0}};
static const StubFn dict_fns[] = {
    {"Load", R_ID}, {"Unload", R_NONE}, {"Lookup", R_ZERO}, {NULL, 0}};

/* ---- bit: 32-bit operations on integer lua_Number ---------------------------------- */

static int bit_bnot(lua_State *L) { lua_pushnumber(L, ~luaL_checkint(L, 1)); return 1; }
static int bit_fold(lua_State *L, int op)
{
    int i, n = lua_gettop(L);
    unsigned v = (unsigned)luaL_checkint(L, 1);
    for (i = 2; i <= n; i++) {
        unsigned x = (unsigned)luaL_checkint(L, i);
        v = op == 0 ? v & x : op == 1 ? v | x : v ^ x;
    }
    lua_pushnumber(L, (int)v);
    return 1;
}
static int bit_band(lua_State *L) { return bit_fold(L, 0); }
static int bit_bor(lua_State *L) { return bit_fold(L, 1); }
static int bit_bxor(lua_State *L) { return bit_fold(L, 2); }
static int bit_lshift(lua_State *L)
{
    lua_pushnumber(L, (int)((unsigned)luaL_checkint(L, 1) << (luaL_checkint(L, 2) & 31)));
    return 1;
}
static int bit_rshift(lua_State *L)
{
    lua_pushnumber(L, (int)((unsigned)luaL_checkint(L, 1) >> (luaL_checkint(L, 2) & 31)));
    return 1;
}
static int bit_arshift(lua_State *L)
{
    lua_pushnumber(L, luaL_checkint(L, 1) >> (luaL_checkint(L, 2) & 31));
    return 1;
}
static int bit_mod(lua_State *L)
{
    int b = luaL_checkint(L, 2);
    lua_pushnumber(L, b ? luaL_checkint(L, 1) % b : 0);
    return 1;
}
static const luaL_reg bit_lib[] = {
    {"bnot", bit_bnot}, {"band", bit_band}, {"bor", bit_bor}, {"bxor", bit_bxor},
    {"lshift", bit_lshift}, {"rshift", bit_rshift}, {"arshift", bit_arshift}, {"mod", bit_mod},
    {NULL, NULL}};

/* ---- zmath ------------------------------------------------------------------------- */

static int zmath_rand(lua_State *L)
{
    int lo = luaL_checkint(L, 1), hi = luaL_checkint(L, 2);
    g_rand = g_rand * 1103515245u + 12345u;
    /* engine: min + rand() % (max - min), i.e. [min, max) */
    lua_pushnumber(L, hi > lo ? lo + (int)((g_rand >> 8) % (unsigned)(hi - lo)) : lo);
    return 1;
}
static int zmath_randseed(lua_State *L) { g_rand = (unsigned)luaL_checkint(L, 1); return 0; }
static const luaL_reg zmath_lib[] = {
    {"Mod", bit_mod}, {"Rand", zmath_rand}, {"RandSeed", zmath_randseed}, {NULL, NULL}};

/* ---- log --------------------------------------------------------------------------- */

static int g_log_level = 3, g_debug_state;
static int log_log(lua_State *L)
{
    int level = luaL_checkint(L, 1);
    if (level <= g_log_level)
        printf("[lua log %d] %s\n", level, luaL_optstring(L, 2, ""));
    return 0;
}
static int log_setlevel(lua_State *L) { g_log_level = luaL_checkint(L, 1); return 0; }
static int log_setmodule(lua_State *L) { return 0; }
static int log_printraw(lua_State *L)
{
    if (g_debug_state)
        printf("%s", luaL_optstring(L, 1, ""));
    return 0;
}
static int log_printline(lua_State *L) { printf("[lua] %s\n", luaL_optstring(L, 1, "")); return 0; }
static int log_debugsetstate(lua_State *L) { g_debug_state = luaL_checkint(L, 1); return 0; }
static const luaL_reg log_lib[] = {
    {"Log", log_log}, {"SetLevel", log_setlevel}, {"SetModule", log_setmodule},
    {"PrintRaw", log_printraw}, {"PrintLine", log_printline}, {"DebugSetState", log_debugsetstate},
    {NULL, NULL}};

static int base_toint(lua_State *L) { lua_pushnumber(L, luaL_checkint(L, 1)); return 1; }

/* ---- registration ----------------------------------------------------------------- */

void zlibs_open(lua_State *L)
{
    luaopen_base(L);      /* includes coroutine in 5.0 */
    luaopen_table(L);
    luaopen_string(L);
    luaopen_debug(L);
    lua_settop(L, 0);
    lua_register(L, "toint", base_toint);
    /* the engine's base lib has no require/dofile/loadfile */
    lua_pushnil(L); lua_setglobal(L, "dofile");
    lua_pushnil(L); lua_setglobal(L, "loadfile");
    lua_pushnil(L); lua_setglobal(L, "require");

    luaL_openlib(L, "bit", bit_lib, 0);
    luaL_openlib(L, "zmath", zmath_lib, 0);
    luaL_openlib(L, "log", log_lib, 0);
    lua_settop(L, 0);
    libgl_open(L);
    libsys_open(L);
    libmedia_open(L);
    libtext_open(L);
    libdata_open(L);
    libeeprom_open(L);

    open_stubs(L, "engine", engine_fns);
    open_stubs(L, "spi", spi_fns);
    open_stubs(L, "uart", uart_fns);
    open_stubs(L, "dict", dict_fns);
    open_stubs(L, "exp_int", exp_int_fns);
}
