/* zbcc: compile Lua 5.0.2 source into a ZAPiT .zbc (the format of game.zbc on Game Wave
 * discs), so homebrew scripts run on the ZIT engine and in Undertow.
 *
 * usage: zbcc out.zbc in1.lua [in2.lua ...]
 * Each input is compiled as its own chunk, so file-level locals stay private and errors
 * name the file; with several inputs a generated main chunk runs them in order. Needs the
 * patched Lua of third_party (integer lua_Number, 32-bit size_t in bytecode). The output
 * is the zlib wrapper ("\x1bZCS\n\x1a", u32 LE unpacked, u32 LE packed, data at +16) around
 * bytecode whose header reads "\x1bZBC\n\x1a" V 01 00 01 E ... (see docs/NOTES.md). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "lauxlib.h"
#include "lua.h"

/* Lua internals, to build the main chunk */
#include "lfunc.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lstring.h"
#include "lundump.h"

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
    return 0;
}

static void wr32le(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

/* Main chunk: CLOSURE 0 i; CALL 0 1 1 for each file chunk (stack slots 1..n), RETURN 0 1.
 * Built after loading, with no allocation that could run the collector in between. */
static Proto *make_main(lua_State *L, int nfiles)
{
    Proto *f = luaF_newproto(L);
    int i, pc = 0;
    f->source = luaS_new(L, "=(zbcc)");
    f->sizep = nfiles;
    f->p = luaM_newvector(L, nfiles, Proto *);
    for (i = 0; i < nfiles; i++)
        f->p[i] = ((const Closure *)lua_topointer(L, i + 1))->l.p;
    f->sizecode = nfiles * 2 + 1;
    f->code = luaM_newvector(L, f->sizecode, Instruction);
    for (i = 0; i < nfiles; i++) {
        f->code[pc++] = CREATE_ABx(OP_CLOSURE, 0, i);
        f->code[pc++] = CREATE_ABC(OP_CALL, 0, 1, 1);
    }
    f->code[pc] = CREATE_ABC(OP_RETURN, 0, 1, 0);
    f->sizelineinfo = f->sizecode;
    f->lineinfo = luaM_newvector(L, f->sizelineinfo, int);
    for (i = 0; i < f->sizelineinfo; i++)
        f->lineinfo[i] = 0;
    f->maxstacksize = 2;
    return f;
}

int main(int argc, char **argv)
{
    lua_State *L;
    Out bc = {0};
    unsigned char *zbc, *packed;
    uLongf plen;
    size_t zlen;
    int i, nfiles = argc - 2;
    FILE *f;

    if (argc < 3) {
        fprintf(stderr, "usage: zbcc out.zbc in1.lua [in2.lua ...]\n");
        return 1;
    }
    L = lua_open();
    for (i = 2; i < argc; i++) {
        Out src = {0};
        char name[512];
        const char *base = strrchr(argv[i], '/'), *b2 = strrchr(argv[i], '\\');
        if (b2 > base)
            base = b2;
        if (add_file(&src, argv[i]) != 0) {
            fprintf(stderr, "zbcc: cannot read %s\n", argv[i]);
            return 1;
        }
        snprintf(name, sizeof(name), "@%s", base ? base + 1 : argv[i]);
        if (luaL_loadbuffer(L, (const char *)src.p, src.n, name) != 0) {
            fprintf(stderr, "zbcc: %s\n", lua_tostring(L, -1));
            return 1;
        }
        free(src.p);
    }
    if (nfiles == 1)
        lua_dump(L, writer, &bc);
    else
        luaU_dump(L, make_main(L, nfiles), writer, &bc);
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
