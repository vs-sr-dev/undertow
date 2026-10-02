#ifndef UNDERTOW_ZLIBS_H
#define UNDERTOW_ZLIBS_H

#include "lua.h"

typedef struct {
    int trace;               /* log stub calls */
    int trace_all;           /* also log idle input polls */
    unsigned long max_calls; /* abort after this many API calls (0 = unlimited) */
    int keys[64];            /* scripted key presses for headless runs */
    int nkeys, key_pos;
} ZlibsConfig;

extern ZlibsConfig zcfg;

void zlibs_open(lua_State *L);

#endif
