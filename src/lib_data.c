/* Lua libraries: pointer (raw byte buffers passed around as light userdata), zfile (disc
 * files). Buffers are tracked so their sizes are known and bogus frees are ignored. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"
#include "lualib.h"
#include "runtime.h"
#include "vfs.h"
#include "zlibs.h"

#define MAX_BUFS 256
#define MAX_FILES 16

typedef struct {
    uint8_t *p;
    size_t n;
} Buf;

static Buf g_bufs[MAX_BUFS];

void *data_buffer_new(size_t n)
{
    int i;
    for (i = 0; i < MAX_BUFS; i++) {
        if (!g_bufs[i].p) {
            g_bufs[i].p = calloc(1, n ? n : 1);
            g_bufs[i].n = n;
            return g_bufs[i].p;
        }
    }
    return NULL;
}

static Buf *find_buf(lua_State *L, int idx)
{
    void *p = lua_touserdata(L, idx);
    int i;
    if (!p)
        return NULL;
    for (i = 0; i < MAX_BUFS; i++)
        if (g_bufs[i].p == p)
            return &g_bufs[i];
    return NULL;
}

/* Buffer and checked byte range [off, off+len) */
static uint8_t *buf_at(lua_State *L, int idx, int off, int len)
{
    Buf *b = find_buf(L, idx);
    if (!b || off < 0 || len < 0 || (size_t)off + (size_t)len > b->n)
        return NULL;
    return b->p + off;
}

static int pt_create(lua_State *L)
{
    void *p = data_buffer_new((size_t)luaL_checkint(L, 1));
    if (p)
        lua_pushlightuserdata(L, p);
    else
        lua_pushnil(L);
    return 1;
}

static int pt_destroy(lua_State *L)
{
    Buf *b = find_buf(L, 1);
    if (b) {
        free(b->p);
        b->p = NULL;
        b->n = 0;
    }
    return 0;
}

static int pt_tostring(lua_State *L)
{
    Buf *b = find_buf(L, 1);
    if (!b) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, (const char *)b->p, strnlen((const char *)b->p, b->n));
    return 1;
}

static int pt_tostringrange(lua_State *L)   /* [min, max) */
{
    int lo = luaL_checkint(L, 2), hi = luaL_checkint(L, 3);
    uint8_t *p = buf_at(L, 1, lo, hi - lo);
    if (!p) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, (const char *)p, (size_t)(hi - lo));
    return 1;
}

static int pt_fromstring(lua_State *L)
{
    size_t n;
    const char *s = luaL_checklstring(L, 1, &n);
    uint8_t *p = data_buffer_new(n + 1);
    if (!p) {
        lua_pushnil(L);
        return 1;
    }
    memcpy(p, s, n);
    lua_pushlightuserdata(L, p);
    return 1;
}

static int pt_getindex(lua_State *L)
{
    uint8_t *p = buf_at(L, 1, luaL_checkint(L, 2), 1);
    lua_pushnumber(L, p ? *p : 0);
    return 1;
}

static int pt_getu32msb(lua_State *L)
{
    uint8_t *p = buf_at(L, 1, luaL_checkint(L, 2), 4);
    lua_pushnumber(L, p ? (int)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]) : 0);
    return 1;
}

static int pt_setu32msb(lua_State *L)
{
    uint8_t *p = buf_at(L, 1, luaL_checkint(L, 2), 4);
    unsigned v = (unsigned)luaL_checkint(L, 3);
    if (p) {
        p[0] = (uint8_t)(v >> 24);
        p[1] = (uint8_t)(v >> 16);
        p[2] = (uint8_t)(v >> 8);
        p[3] = (uint8_t)v;
    }
    return 0;
}

static int pt_getu32lsb(lua_State *L)
{
    uint8_t *p = buf_at(L, 1, luaL_checkint(L, 2), 4);
    lua_pushnumber(L, p ? (int)(p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24)) : 0);
    return 1;
}

static int pt_setu32lsb(lua_State *L)
{
    uint8_t *p = buf_at(L, 1, luaL_checkint(L, 2), 4);
    unsigned v = (unsigned)luaL_checkint(L, 3);
    if (p) {
        p[0] = (uint8_t)v;
        p[1] = (uint8_t)(v >> 8);
        p[2] = (uint8_t)(v >> 16);
        p[3] = (uint8_t)(v >> 24);
    }
    return 0;
}

static int pt_bytetoascii(lua_State *L)
{
    char c = (char)luaL_checkint(L, 1);
    lua_pushlstring(L, &c, 1);
    return 1;
}

static int pt_asciitobyte(lua_State *L)
{
    const char *s = luaL_checkstring(L, 1);
    lua_pushnumber(L, (unsigned char)s[0]);
    return 1;
}

static const luaL_reg pointer_lib[] = {
    {"CreateUserData", pt_create}, {"DestroyUserData", pt_destroy}, {"ToString", pt_tostring},
    {"ToStringRange", pt_tostringrange}, {"FromString", pt_fromstring},
    {"GetIndex", pt_getindex}, {"GetU32MSB", pt_getu32msb}, {"SetU32MSB", pt_setu32msb},
    {"GetU32LSB", pt_getu32lsb}, {"SetU32LSB", pt_setu32lsb}, {"ByteToAscii", pt_bytetoascii},
    {"AsciiToByte", pt_asciitobyte}, {NULL, NULL}};

/* ---- zfile ---- */

typedef struct {
    int id;
    VfsFile *f;
} ZFile;

static ZFile g_files[MAX_FILES];

static ZFile *find_file(int id)
{
    int i;
    for (i = 0; i < MAX_FILES; i++)
        if (g_files[i].id == id && id)
            return &g_files[i];
    return NULL;
}

static int zf_open(lua_State *L)   /* OpenFile(path [, mode, loc]) -> id or -1 */
{
    VfsFile *f;
    int i;
    rt_trace_call(L, "zfile.OpenFile");
    f = vfs_open(luaL_checkstring(L, 1));
    for (i = 0; f && i < MAX_FILES; i++) {
        if (!g_files[i].id) {
            g_files[i].id = rt_new_id();
            g_files[i].f = f;
            lua_pushnumber(L, g_files[i].id);
            return 1;
        }
    }
    vfs_close(f);
    lua_pushnumber(L, -1);
    return 1;
}

static int zf_readbytes(lua_State *L)   /* ReadBytes(id, offset, count) -> buffer or nil */
{
    ZFile *z = find_file(luaL_checkint(L, 1));
    int off = luaL_checkint(L, 2), n = luaL_checkint(L, 3);
    uint8_t *p;
    if (!z || off < 0 || n < 0 || vfs_seek(z->f, (uint64_t)off) != 0 ||
        !(p = data_buffer_new((size_t)n))) {
        lua_pushnil(L);
        return 1;
    }
    vfs_read(z->f, p, (size_t)n);
    lua_pushlightuserdata(L, p);
    return 1;
}

static int zf_readline(lua_State *L)    /* ReadLine(id) -> 0, line | 1, nil */
{
    ZFile *z = find_file(luaL_checkint(L, 1));
    luaL_Buffer b;
    char c;
    int got = 0;
    if (!z) {
        lua_pushnumber(L, 1);
        lua_pushnil(L);
        return 2;
    }
    luaL_buffinit(L, &b);
    while (vfs_read(z->f, &c, 1) == 1) {
        got = 1;
        if (c == '\n')
            break;
        if (c != '\r')
            luaL_putchar(&b, c);
    }
    luaL_pushresult(&b);
    if (!got) {
        lua_pop(L, 1);
        lua_pushnumber(L, 1);
        lua_pushnil(L);
        return 2;
    }
    lua_pushnumber(L, 0);
    lua_insert(L, -2);
    return 2;
}

static int zf_sethead(lua_State *L)
{
    ZFile *z = find_file(luaL_checkint(L, 1));
    lua_pushnumber(L, z && vfs_seek(z->f, (uint64_t)luaL_checkint(L, 2)) == 0 ? 0 : -1);
    return 1;
}

static int zf_size(lua_State *L)
{
    ZFile *z = find_file(luaL_checkint(L, 1));
    lua_pushnumber(L, z ? (int)vfs_size(z->f) : -1);
    return 1;
}

static int zf_close(lua_State *L)
{
    ZFile *z = find_file(luaL_checkint(L, 1));
    if (z) {
        vfs_close(z->f);
        z->id = 0;
    }
    lua_pushnumber(L, 0);
    return 1;
}

static const luaL_reg zfile_lib[] = {
    {"OpenFile", zf_open}, {"ReadBytes", zf_readbytes}, {"ReadLine", zf_readline},
    {"SetHeadPosition", zf_sethead}, {"GetFileSize", zf_size}, {"CloseFile", zf_close},
    {NULL, NULL}};

void libdata_open(lua_State *L)
{
    luaL_openlib(L, "pointer", pointer_lib, 0);
    luaL_openlib(L, "zfile", zfile_lib, 0);
    lua_settop(L, 0);
}
