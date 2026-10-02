/* ZIT engine Lua libraries.
 * Simple libraries are implemented for real; the rest are tracing stubs that log each call
 * and return plausible values, so scripts can be run and observed while the real
 * implementations are written. Function lists mirror the engine's luaL_reg tables
 * (docs/engine_api_tables.txt). */
#include "zlibs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lualib.h"

ZlibsConfig zcfg;
static unsigned long g_calls;
static int g_next_id = 1;
static unsigned g_rand = 12345;

/* ---- helpers ---------------------------------------------------------------------- */

static void trace_args(lua_State *L, char *buf, size_t size)
{
    int i, n = lua_gettop(L);
    size_t len = 0;
    buf[0] = 0;
    for (i = 1; i <= n && len + 40 < size; i++) {
        const char *sep = i > 1 ? ", " : "";
        switch (lua_type(L, i)) {
        case LUA_TNUMBER:
            len += snprintf(buf + len, size - len, "%s%d", sep, (int)lua_tonumber(L, i));
            break;
        case LUA_TSTRING:
            len += snprintf(buf + len, size - len, "%s\"%.60s\"", sep, lua_tostring(L, i));
            break;
        case LUA_TBOOLEAN:
            len += snprintf(buf + len, size - len, "%s%s", sep, lua_toboolean(L, i) ? "true" : "false");
            break;
        case LUA_TNIL:
            len += snprintf(buf + len, size - len, "%snil", sep);
            break;
        default:
            len += snprintf(buf + len, size - len, "%s<%s>", sep, lua_typename(L, lua_type(L, i)));
        }
    }
}

static void count_call(lua_State *L)
{
    if (zcfg.max_calls && ++g_calls > zcfg.max_calls)
        luaL_error(L, "undertow: call budget (%d) exhausted", (int)zcfg.max_calls);
}

/* ---- tracing stubs ---------------------------------------------------------------- */

/* Return kinds for stubs. */
enum { R_NONE, R_ID, R_ZERO, R_ONE, R_POS, R_KEY, R_NILZERO, R_MEM, R_VERSION, R_STRS,
       R_MOVIE_PLAY, R_MOVIE_STATE };

static unsigned g_now_ms;
static int g_movie_polls;   /* stub movie: 'plays' for this many GetState polls */

typedef struct {
    const char *name;
    int ret;
} StubFn;

static int stub_call(lua_State *L)
{
    const char *name = lua_tostring(L, lua_upvalueindex(1));
    int ret = (int)lua_tonumber(L, lua_upvalueindex(2));
    char args[512];
    int nret = 0;

    count_call(L);
    trace_args(L, args, sizeof(args));
    switch (ret) {
    case R_ID: lua_pushnumber(L, g_next_id++); nret = 1; break;
    case R_ZERO: lua_pushnumber(L, 0); nret = 1; break;
    case R_ONE: lua_pushnumber(L, 1); nret = 1; break;
    case R_POS: lua_pushnumber(L, 0); lua_pushnumber(L, 0); nret = 2; break;
    case R_KEY: {
        int key = 255;
        if (zcfg.nkeys && zcfg.key_pos < zcfg.nkeys && (g_calls % 50) == 0)
            key = zcfg.keys[zcfg.key_pos++];
        /* key_id, remote_id, timestamp */
        lua_pushnumber(L, key); lua_pushnumber(L, 1); lua_pushnumber(L, (int)g_now_ms); nret = 3;
        if (key == 255 && !zcfg.trace_all)
            return nret;   /* don't flood the log with idle polls */
        break;
    }
    case R_NILZERO: lua_pushnil(L); lua_pushnumber(L, 0); nret = 2; break;
    case R_MEM: lua_pushnumber(L, 8 << 20); nret = 1; break;
    case R_VERSION: lua_pushstring(L, "0.11.3.undertow"); nret = 1; break;
    case R_STRS: lua_pushstring(L, ""); lua_pushstring(L, ""); lua_pushnumber(L, 0); nret = 3; break;
    case R_MOVIE_PLAY: g_movie_polls = 30; break;
    case R_MOVIE_STATE: /* 0 = finished (sudoku Wait_For_Movie) */
        lua_pushnumber(L, g_movie_polls > 0 ? (g_movie_polls--, 1) : 0); nret = 1;
        if (g_movie_polls > 0 && !zcfg.trace_all)
            return nret;
        break;
    }
    if (zcfg.trace) {
        if (nret == 1 && lua_isnumber(L, -1))
            printf("[%6lu] %s(%s) -> %d\n", g_calls, name, args, (int)lua_tonumber(L, -1));
        else
            printf("[%6lu] %s(%s)\n", g_calls, name, args);
    }
    return nret;
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

static const StubFn gl_fns[] = {
    {"SelectOSDMode", R_NONE}, {"BeginScene", R_NONE}, {"EndScene", R_NONE},
    {"CreateOverlayFromTexture", R_ID}, {"FreeOverlay", R_NONE}, {"LoadTexture", R_ID},
    {"FreeTexture", R_NONE}, {"AddTextureToOverlay", R_NONE}, {"RemoveTextureFromOverlay", R_NONE},
    {"SetTextureActiveFrame", R_NONE}, {"SetClipInfo", R_NONE}, {"SetParameters", R_NONE},
    {"SetZorder", R_NONE}, {"SetVisibility", R_NONE}, {"SetPosition", R_NONE},
    {"GetZorder", R_ZERO}, {"GetVisibility", R_ZERO}, {"GetPosition", R_POS}, {"GetSize", R_POS},
    {"ClearOSD", R_NONE}, {"Show", R_NONE}, {"HasAnimations", R_ZERO},
    {"DeleteAllAnimations", R_NONE}, {"AddPositionAnimation", R_NONE},
    {"AddVisibilityAnimation", R_NONE}, {"AddParabolaAnimation", R_NONE},
    {"AddBlinkingAnimation", R_NONE}, {"AddAlphaAnimation", R_NONE},
    {"CreateTextureAnimation", R_ID}, {"CreateEmptyTexture", R_ID}, {"BlitOverlay", R_NONE},
    {"BlitOverlayWithCR", R_NONE}, {"SetTextureAlphaLevel", R_NONE}, {NULL, 0}};
static const StubFn iframe_fns[] = {
    {"Load", R_ID}, {"Unload", R_NONE}, {"Show", R_NONE}, {"ShowPredefined", R_NONE},
    {"Clear", R_NONE}, {NULL, 0}};
static const StubFn input_fns[] = {
    {"ClearKeyQueue", R_NONE}, {"GetKey", R_KEY}, {"WaitForKey", R_KEY},
    {"EnableRemotes", R_ONE}, {"DisableRemotes", R_ONE}, {"SetMode", R_NONE},
    {"GetMode", R_ZERO}, {"SetQueueSize", R_NONE}, {"SetRandomKeysTable", R_NONE}, {NULL, 0}};
static const StubFn pointer_fns[] = {
    {"CreateUserData", R_ID}, {"DestroyUserData", R_NONE}, {"ToString", R_ZERO},
    {"ToStringRange", R_ZERO}, {"FromString", R_ID}, {"GetIndex", R_ZERO},
    {"GetU32MSB", R_ZERO}, {"SetU32MSB", R_NONE}, {"GetU32LSB", R_ZERO}, {"SetU32LSB", R_NONE},
    {"ByteToAscii", R_ZERO}, {"AsciiToByte", R_ZERO}, {NULL, 0}};
static const StubFn rm_fns[] = {
    {"OpenResource", R_ID}, {"CloseResource", R_NONE}, {"LoadFile", R_NILZERO},
    {"UnloadFile", R_NONE}, {NULL, 0}};
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
static const StubFn font_fns[] = {
    {"Load", R_ID}, {"Free", R_NONE}, {"GetBuiltinFontID", R_ID}, {NULL, 0}};
static const StubFn text_fns[] = {
    {"RenderSimple", R_ID}, {"Render", R_ID}, {"Remove", R_NONE}, {"GetOverlayId", R_ID},
    {NULL, 0}};
static const StubFn movie_fns[] = {
    {"Load", R_ID}, {"SetLoop", R_NONE}, {"Play", R_MOVIE_PLAY}, {"Stop", R_NONE},
    {"Resume", R_NONE}, {"GetState", R_MOVIE_STATE}, {NULL, 0}};
static const StubFn audio_fns[] = {
    {"Load", R_ID}, {"Unload", R_NONE}, {"Play", R_NONE}, {NULL, 0}};
static const StubFn spi_fns[] = {
    {"Init", R_ZERO}, {"Open", R_ZERO}, {"Close", R_ZERO}, {"Write", R_ZERO}, {"Read", R_ZERO},
    {NULL, 0}};
static const StubFn exp_int_fns[] = {
    {"Configure", R_ZERO}, {"Close", R_ZERO}, {"Test", R_ZERO}, {NULL, 0}};
static const StubFn uart_fns[] = {
    {"RxD", R_ZERO}, {"TxD", R_ZERO}, {"Open", R_ONE}, {"Close", R_ONE}, {NULL, 0}};
static const StubFn eeprom_fns[] = {
    {"SaveGameToNewSlot", R_ONE}, {"GetSaveNameByID", R_STRS}, {"LoadSaveByID", R_NILZERO},
    {"UnloadData", R_NONE}, {"SaveGameToExistingSlot", R_ONE},
    {"EnumerateGameSavesByID", R_POS}, {"EnumerateGameSavesByName", R_POS},
    {"Format", R_NONE}, {"CorruptFlash", R_NONE}, {"CheckFlashIntegrity", R_ONE}, {NULL, 0}};
static const StubFn zfile_fns[] = {
    {"OpenFile", R_ID}, {"ReadBytes", R_NILZERO}, {"ReadLine", R_NILZERO},
    {"SetHeadPosition", R_ZERO}, {"GetFileSize", R_ZERO}, {"CloseFile", R_ZERO}, {NULL, 0}};
static const StubFn dict_fns[] = {
    {"Load", R_ID}, {"Unload", R_NONE}, {"Lookup", R_ZERO}, {NULL, 0}};

/* ---- real implementations --------------------------------------------------------- */

/* bit: 32-bit operations on integer lua_Number */
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

/* zmath */
static int zmath_mod(lua_State *L) { return bit_mod(L); }
static int zmath_rand(lua_State *L)
{
    int lo = luaL_checkint(L, 1), hi = luaL_checkint(L, 2);
    g_rand = g_rand * 1103515245u + 12345u;
    lua_pushnumber(L, hi >= lo ? lo + (int)((g_rand >> 8) % (unsigned)(hi - lo + 1)) : lo);
    return 1;
}
static int zmath_randseed(lua_State *L) { g_rand = (unsigned)luaL_checkint(L, 1); return 0; }
static const luaL_reg zmath_lib[] = {
    {"Mod", zmath_mod}, {"Rand", zmath_rand}, {"RandSeed", zmath_randseed}, {NULL, NULL}};

/* time: virtual clock in milliseconds, advanced by Sleep (headless runs go fast) */
static int time_getrealtime(lua_State *L)
{
    count_call(L);
    g_now_ms += 1;
    lua_pushnumber(L, (int)g_now_ms);
    return 1;
}
static int time_sleep(lua_State *L)
{
    count_call(L);
    g_now_ms += (unsigned)luaL_optint(L, 1, 0);
    return 0;
}
static const luaL_reg time_lib[] = {
    {"GetRealTime", time_getrealtime}, {"Sleep", time_sleep}, {NULL, NULL}};

/* log */
static int g_log_level = 3, g_debug_state;
static int log_log(lua_State *L)
{
    int level = luaL_checkint(L, 1);
    if (level <= g_log_level)
        printf("[lua log %d] %s\n", level, luaL_optstring(L, 2, ""));
    return 0;
}
static int log_setlevel(lua_State *L) { g_log_level = luaL_checkint(L, 1); return 0; }
static int log_setmodule(lua_State *L) { (void)L; return 0; }
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

/* base additions */
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
    luaL_openlib(L, "time", time_lib, 0);
    luaL_openlib(L, "log", log_lib, 0);
    lua_settop(L, 0);

    open_stubs(L, "gl", gl_fns);
    open_stubs(L, "rm", rm_fns);
    open_stubs(L, "pointer", pointer_fns);
    open_stubs(L, "input", input_fns);
    open_stubs(L, "iframe", iframe_fns);
    open_stubs(L, "engine", engine_fns);
    open_stubs(L, "text", text_fns);
    open_stubs(L, "font", font_fns);
    open_stubs(L, "movie", movie_fns);
    open_stubs(L, "audio", audio_fns);
    open_stubs(L, "spi", spi_fns);
    open_stubs(L, "uart", uart_fns);
    open_stubs(L, "eeprom", eeprom_fns);
    open_stubs(L, "zfile", zfile_fns);
    open_stubs(L, "dict", dict_fns);
    open_stubs(L, "exp_int", exp_int_fns);
}
