/* Undertow - ZAPiT Game Wave emulator (HLE of the ZIT Lua engine). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diz.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "vfs.h"
#include "zbc.h"
#include "zlibs.h"

static void usage(void)
{
    fprintf(stderr,
            "usage: undertow <disc.iso | disc_dir> [options]\n"
            "  --trace            log engine API calls\n"
            "  --trace-all        also log idle input polls\n"
            "  --max-calls N      stop after N API calls\n"
            "  --keys k1,k2,...   scripted key codes for headless runs\n");
}

static int traceback(lua_State *L)
{
    lua_getglobal(L, "debug");
    lua_pushstring(L, "traceback");
    lua_gettable(L, -2);
    lua_pushvalue(L, 1);
    lua_call(L, 1, 1);
    return 1;
}

int main(int argc, char **argv)
{
    const char *disc = NULL;
    unsigned char *buf;
    size_t size;
    Diz diz;
    lua_State *L;
    int i, rc;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--trace"))
            zcfg.trace = 1;
        else if (!strcmp(argv[i], "--trace-all"))
            zcfg.trace = zcfg.trace_all = 1;
        else if (!strcmp(argv[i], "--max-calls") && i + 1 < argc)
            zcfg.max_calls = strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--keys") && i + 1 < argc) {
            char *s = argv[++i];
            while (*s && zcfg.nkeys < 64) {
                zcfg.keys[zcfg.nkeys++] = (int)strtol(s, &s, 10);
                if (*s == ',')
                    s++;
            }
        } else if (argv[i][0] != '-' && !disc)
            disc = argv[i];
        else {
            usage();
            return 1;
        }
    }
    if (!disc) {
        usage();
        return 1;
    }
    if (vfs_mount(disc) != 0) {
        fprintf(stderr, "cannot mount %s\n", disc);
        return 1;
    }
    buf = vfs_read_all("gamewave.diz", &size);
    if (!buf) {
        fprintf(stderr, "no gamewave.diz in %s\n", disc);
        return 1;
    }
    buf = realloc(buf, size + 1);
    buf[size] = 0;
    if (diz_parse((const char *)buf, &diz) != 0) {
        fprintf(stderr, "bad gamewave.diz\n");
        return 1;
    }
    free(buf);
    printf("Game: %s (version %s), engine %s board %d (%s)\n", diz.appname, diz.version,
           diz.engine, diz.board, diz.engine_version);

    buf = vfs_read_all(diz.appfile, &size);
    if (!buf) {
        fprintf(stderr, "cannot read %s\n", diz.appfile);
        return 1;
    }
    L = lua_open();
    zlibs_open(L);
    lua_pushcfunction(L, traceback);
    rc = zbc_load(L, buf, size, diz.appfile);
    free(buf);
    if (rc != 0) {
        fprintf(stderr, "load error: %s\n", lua_tostring(L, -1));
        return 1;
    }
    rc = lua_pcall(L, 0, 0, 1);
    if (rc != 0)
        fprintf(stderr, "script error: %s\n", lua_tostring(L, -1));
    else
        printf("script finished\n");
    lua_close(L);
    return rc ? 2 : 0;
}
