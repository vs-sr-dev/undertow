#ifndef UNDERTOW_ZLIBS_H
#define UNDERTOW_ZLIBS_H

#include "lua.h"

/* Register every engine library into L. */
void zlibs_open(lua_State *L);

/* per-library registration (lib_*.c) */
void libgl_open(lua_State *L);    /* gl, rm, iframe */
void libsys_open(lua_State *L);   /* input, time */
void libmedia_open(lua_State *L); /* movie, audio */
void libtext_open(lua_State *L);  /* font, text */
void libdata_open(lua_State *L);  /* pointer, zfile */

#endif
