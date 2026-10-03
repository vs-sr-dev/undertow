/* Lua library: eeprom (save games). Argument and return conventions follow the engine's
 * apps/zit/zap_lua/zlua_eeprom.c (0x806295f4..0x806299dc): the trailing boolean of most calls
 * is an error flag (false on success), which is what the scripts test. */
#include <stdlib.h>
#include <string.h>

#include "eeprom.h"
#include "lauxlib.h"
#include "lualib.h"
#include "runtime.h"
#include "zlibs.h"

/* Bytes [0, size) of a script buffer, zero-padded when the buffer is shorter (and to at
 * least 2 bytes: the manager always stores 2 in the first page). */
static uint8_t *copy_data(lua_State *L, int idx, int size)
{
    void *p = lua_touserdata(L, idx);
    size_t have = data_buffer_size(p);
    uint8_t *d = calloc(1, (size_t)size + 2);
    if (p && have)
        memcpy(d, p, have < (size_t)size ? have : (size_t)size);
    return d;
}

/* SaveGameToNewSlot(app_id, size, app_name, save_name, data) -> failed */
static int ee_save_new(lua_State *L)
{
    EepHeader h;
    uint8_t *d;
    int ok;

    rt_trace_call(L, "eeprom.SaveGameToNewSlot");
    memset(&h, 0, sizeof(h));
    h.app_id = luaL_checkint(L, 1) & 0xffff;
    h.size = luaL_checkint(L, 2) & 0xffff;
    strncpy(h.app_name, luaL_checkstring(L, 3), 16);
    strncpy(h.save_name, luaL_checkstring(L, 4), 32);
    d = copy_data(L, 5, h.size);
    ok = eep_save_new(&h, d);
    free(d);
    lua_pushboolean(L, !ok);
    return 1;
}

/* SaveGameToExistingSlot(id, data) -> false (the engine ignores the manager's result) */
static int ee_save_existing(lua_State *L)
{
    int id = luaL_checkint(L, 1);
    void *p = lua_touserdata(L, 2);
    size_t n = data_buffer_size(p);
    uint8_t *d;

    rt_trace_call(L, "eeprom.SaveGameToExistingSlot");
    d = calloc(1, EEP_SIZE);    /* the manager reads as many bytes as the save holds */
    if (p && n)
        memcpy(d, p, n < EEP_SIZE ? n : EEP_SIZE);
    eep_save_existing(id, d);
    free(d);
    lua_pushboolean(L, 0);
    return 1;
}

/* EnumerateGameSavesByID(app_id) / ByName(app_name) -> count, false */
static int ee_enum_id(lua_State *L)
{
    rt_trace_call(L, "eeprom.EnumerateGameSavesByID");
    lua_pushnumber(L, eep_enum_by_id(luaL_checkint(L, 1) & 0xffff));
    lua_pushboolean(L, 0);
    return 2;
}

static int ee_enum_name(lua_State *L)
{
    char name[17] = {0};
    rt_trace_call(L, "eeprom.EnumerateGameSavesByName");
    strncpy(name, luaL_checkstring(L, 1), 16);
    lua_pushnumber(L, eep_enum_by_name(name));
    lua_pushboolean(L, 0);
    return 2;
}

/* GetSaveNameByID(id) -> app_name, save_name, false */
static int ee_names(lua_State *L)
{
    int id = luaL_checkint(L, 1);
    rt_trace_call(L, "eeprom.GetSaveNameByID");
    lua_pushstring(L, eep_app_name(id));
    lua_pushstring(L, eep_save_name(id));
    lua_pushboolean(L, 0);
    return 3;
}

/* LoadSaveByID(id) -> data, failed. data is a pointer buffer (scripts read it with pointer.*
 * and release it with UnloadData or pointer.DestroyUserData). */
static int ee_load(lua_State *L)
{
    uint8_t *d = NULL, *buf = NULL;
    int size = 0, ok;

    rt_trace_call(L, "eeprom.LoadSaveByID");
    ok = eep_load(luaL_checkint(L, 1), &d, &size);   /* data comes back even on CRC failure */
    if (d && (buf = data_buffer_new((size_t)size)) != NULL)
        memcpy(buf, d, (size_t)size);
    free(d);
    if (buf)
        lua_pushlightuserdata(L, buf);
    else
        lua_pushnil(L);
    lua_pushboolean(L, !ok || !buf);
    return 2;
}

static int ee_unload(lua_State *L)
{
    rt_trace_call(L, "eeprom.UnloadData");
    data_buffer_free(lua_touserdata(L, 1));
    return 0;
}

static int ee_format(lua_State *L)
{
    rt_trace_call(L, "eeprom.Format");
    eep_format();
    return 0;
}

static int ee_corrupt(lua_State *L)
{
    rt_trace_call(L, "eeprom.CorruptFlash");
    eep_corrupt();
    return 0;
}

static int ee_check(lua_State *L)
{
    rt_trace_call(L, "eeprom.CheckFlashIntegrity");
    lua_pushnumber(L, eep_check());
    return 1;
}

static const luaL_reg eeprom_lib[] = {
    {"SaveGameToNewSlot", ee_save_new}, {"GetSaveNameByID", ee_names},
    {"LoadSaveByID", ee_load}, {"UnloadData", ee_unload},
    {"SaveGameToExistingSlot", ee_save_existing}, {"EnumerateGameSavesByID", ee_enum_id},
    {"EnumerateGameSavesByName", ee_enum_name}, {"Format", ee_format},
    {"CorruptFlash", ee_corrupt}, {"CheckFlashIntegrity", ee_check}, {NULL, NULL}};

void libeeprom_open(lua_State *L)
{
    luaL_openlib(L, "eeprom", eeprom_lib, 0);
    lua_settop(L, 0);
}
