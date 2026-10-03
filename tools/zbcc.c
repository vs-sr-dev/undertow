/* zbcc: compile Lua 5.0.2 source into a ZAPiT .zbc (the format of game.zbc on Game Wave
 * discs), so homebrew scripts run on the ZIT engine and in Undertow.
 *
 * usage: zbcc out.zbc in1.lua [in2.lua ...]
 * Several inputs are concatenated into one chunk, in order. Needs the patched Lua of
 * third_party (integer lua_Number, 32-bit size_t in bytecode). The output is the zlib
 * wrapper ("\x1bZCS\n\x1a", u32 LE unpacked, u32 LE packed, data at +16) around bytecode
 * whose header reads "\x1bZBC\n\x1a" V 01 00 01 E ... (see docs/NOTES.md). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "lauxlib.h"
#include "lua.h"

typedef struct {
    unsigned char *p;
    size_t n, cap;
} Out;

static int writer(lua_State *L, const void *p, size_t n, void *ud)
{
    Out *o = ud;
    (void)L;
    if (o->n + n > o->cap) {
        o->cap = (o->n + n) * 2;
        o->p = realloc(o->p, o->cap);
    }
    memcpy(o->p + o->n, p, n);
    o->n += n;
    return 1;
}

static int add_file(Out *o, const char *path)
{
    FILE *f = fopen(path, "rb");
    unsigned char buf[65536];
    size_t n;
    if (!f)
        return -1;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        writer(NULL, buf, n, o);
    fclose(f);
    writer(NULL, "\n", 1, o);
    return 0;
}

static void wr32le(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

int main(int argc, char **argv)
{
    lua_State *L;
    Out src = {0}, bc = {0};
    unsigned char *zbc, *packed;
    char name[512];
    uLongf plen;
    size_t zlen;
    int i;
    FILE *f;

    if (argc < 3) {
        fprintf(stderr, "usage: zbcc out.zbc in1.lua [in2.lua ...]\n");
        return 1;
    }
    for (i = 2; i < argc; i++) {
        if (add_file(&src, argv[i]) != 0) {
            fprintf(stderr, "zbcc: cannot read %s\n", argv[i]);
            return 1;
        }
    }
    snprintf(name, sizeof(name), argc == 3 ? "@%s" : "=(zbcc)", argv[2]);
    L = lua_open();
    if (luaL_loadbuffer(L, (const char *)src.p, src.n, name) != 0) {
        fprintf(stderr, "zbcc: %s\n", lua_tostring(L, -1));
        return 1;
    }
    lua_dump(L, writer, &bc);
    lua_close(L);

    /* "\x1bLua" V E ...  ->  "\x1bZBC\n\x1a" V 01 00 01 E ... */
    zlen = bc.n + 5;
    zbc = malloc(zlen);
    memcpy(zbc, "\x1bZBC\n\x1a", 6);
    zbc[6] = bc.p[4];
    zbc[7] = 1;
    zbc[8] = 0;
    zbc[9] = 1;
    memcpy(zbc + 10, bc.p + 5, bc.n - 5);

    plen = compressBound((uLong)zlen);
    packed = malloc(16 + plen);
    if (compress2(packed + 16, &plen, zbc, (uLong)zlen, 9) != Z_OK) {
        fprintf(stderr, "zbcc: zlib error\n");
        return 1;
    }
    memcpy(packed, "\x1bZCS\n\x1a\0\0", 8);
    wr32le(packed + 8, (unsigned)zlen);
    wr32le(packed + 12, (unsigned)plen);
    f = fopen(argv[1], "wb");
    if (!f || fwrite(packed, 1, 16 + plen, f) != 16 + plen) {
        fprintf(stderr, "zbcc: cannot write %s\n", argv[1]);
        return 1;
    }
    fclose(f);
    return 0;
}
