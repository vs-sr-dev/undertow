/* .zbc loader: optional zlib wrapper ("\x1bZCS\n\x1a", u32 unpacked, u32 packed, data at +16)
 * around Lua 5.0.2 bytecode with a ZAPiT header ("\x1bZBC\n\x1a", version, 3 extra bytes,
 * endianness, sizes...). The header is rewritten to the stock "\x1bLua" form. */
#include "zbc.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "lauxlib.h"

static const unsigned char ZCS_MAGIC[6] = {0x1b, 'Z', 'C', 'S', '\n', 0x1a};
static const unsigned char ZBC_MAGIC[6] = {0x1b, 'Z', 'B', 'C', '\n', 0x1a};

static unsigned rd32le(const unsigned char *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

int zbc_load(lua_State *L, const unsigned char *data, size_t size, const char *name)
{
    unsigned char *raw = NULL, *chunk;
    size_t rawsize = size;
    int rc;

    if (size >= 16 && memcmp(data, ZCS_MAGIC, 6) == 0) {
        uLongf outlen = rd32le(data + 8);
        raw = malloc(outlen ? outlen : 1);
        if (!raw || uncompress(raw, &outlen, data + 16, (uLong)(size - 16)) != Z_OK) {
            free(raw);
            lua_pushfstring(L, "%s: zlib error", name);
            return LUA_ERRSYNTAX;
        }
        data = raw;
        rawsize = outlen;
    }
    if (rawsize < 12 || memcmp(data, ZBC_MAGIC, 6) != 0) {
        free(raw);
        lua_pushfstring(L, "%s: not ZAPiT bytecode", name);
        return LUA_ERRSYNTAX;
    }
    /* "\x1bZBC\n\x1a" V x x x E rest...  ->  "\x1bLua" V E rest... */
    chunk = malloc(rawsize);
    memcpy(chunk, "\x1bLua", 4);
    chunk[4] = data[6];
    chunk[5] = data[10];
    memcpy(chunk + 6, data + 11, rawsize - 11);
    rc = luaL_loadbuffer(L, (const char *)chunk, rawsize - 5, name);
    free(chunk);
    free(raw);
    return rc;
}
