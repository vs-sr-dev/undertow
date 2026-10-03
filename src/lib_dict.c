/* Lua library: dict (word lists, Letter Zap). Follows the engine's dict bindings
 * (0x80629d50.., core 0x80624a64..); format in docs/NOTES.md "Dictionary .zdt".
 * A .zdt is a trie after an 8-byte header: node = u8 edge count + edges; edge = byte
 * (bits0-4 letter code, char = code|0x60; bit5 end of word; bits6-7 K) + K-byte LE
 * offset of the child node from the start of this node. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lualib.h"
#include "runtime.h"
#include "vfs.h"
#include "zlibs.h"

typedef struct {
    uint8_t *data;
    size_t size;
} Dict;

/* The engine's walk (0x80624ad0), with bounds checks: edges are scanned in file order
 * (the Latin list is not sorted), "" counts as found. */
static int dict_lookup(const Dict *d, const uint8_t *w)
{
    size_t node = 8;
    if (!*w)
        return 1;
    while (node < d->size) {
        size_t p = node + 1;
        int count = d->data[node], k;
        uint32_t off;
        for (; count > 0 && p < d->size; count--) {
            if (((d->data[p] & 0x1f) | 0x60) == *w)
                break;
            p += (size_t)(d->data[p] >> 6) + 1;
        }
        if (count <= 0 || p >= d->size)
            return 0;
        if ((d->data[p] & 0x20) && !w[1])
            return 1;
        k = d->data[p] >> 6;
        if (!k || p + (size_t)k >= d->size)
            return 0;
        off = d->data[p + 1];
        if (k >= 2)
            off |= (uint32_t)d->data[p + 2] << 8;
        if (k == 3)
            off |= (uint32_t)d->data[p + 3] << 16;
        node += off;
        w++;
    }
    return 0;
}

/* Load(res, name) -> handle. Like the engine, a failed load still returns a light
 * userdata (NULL), never nil. */
static int dc_load(lua_State *L)
{
    char path[512];
    Dict *d = NULL;
    uint8_t *data;
    size_t size;

    rt_trace_call(L, "dict.Load");
    rm_join_path(path, sizeof(path), luaL_checkint(L, 1), luaL_checkstring(L, 2));
    data = vfs_read_all(path, &size);
    if (data) {
        d = malloc(sizeof(*d));
        d->data = data;
        d->size = size;
    } else {
        fprintf(stderr, "Cannot open dictionary : %s\n", path);
    }
    lua_pushlightuserdata(L, d);
    return 1;
}

static int dc_unload(lua_State *L)
{
    Dict *d = lua_touserdata(L, 1);
    rt_trace_call(L, "dict.Unload");
    if (d) {
        free(d->data);
        free(d);
    }
    return 0;
}

/* Lookup(handle, word) -> boolean (exact, case-sensitive; scripts lowercase first) */
static int dc_lookup(lua_State *L)
{
    Dict *d = lua_touserdata(L, 1);
    const char *w = luaL_checkstring(L, 2);
    int found = d ? dict_lookup(d, (const uint8_t *)w) : 0;
    if (rt_trace)
        printf("[%7u] dict.Lookup(\"%s\") -> %s\n", (unsigned)rt_now_ms(), w,
               found ? "true" : "false");
    lua_pushboolean(L, found);
    return 1;
}

static const luaL_reg dict_lib[] = {
    {"Load", dc_load}, {"Unload", dc_unload}, {"Lookup", dc_lookup}, {NULL, NULL}};

void libdict_open(lua_State *L)
{
    luaL_openlib(L, "dict", dict_lib, 0);
    lua_settop(L, 0);
}
