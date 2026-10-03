#ifndef UNDERTOW_ZLIBS_H
#define UNDERTOW_ZLIBS_H

#include <stddef.h>

#include "lua.h"

/* Register every engine library into L. */
void zlibs_open(lua_State *L);

/* per-library registration (lib_*.c) */
void libgl_open(lua_State *L);    /* gl, rm, iframe */
void libsys_open(lua_State *L);   /* input, time */
void libmedia_open(lua_State *L); /* movie, audio */
void libtext_open(lua_State *L);  /* font, text */
void libdata_open(lua_State *L);  /* pointer, zfile */
void libeeprom_open(lua_State *L); /* eeprom */
void libdict_open(lua_State *L);  /* dict */

/* Entry `name` of the file table in an engine binary d[0..n), lib_text.c */
const unsigned char *cheese_find(const unsigned char *d, size_t n, const char *name,
                                 size_t *size);

/* "<resource dir>/<name>" for a handle from rm.OpenResource, lib_gl.c */
void rm_join_path(char *out, size_t size, int res, const char *name);

/* pointer buffers (light userdata the scripts pass around), lib_data.c */
void *data_buffer_new(size_t n);  /* zero-filled; NULL when the table is full */
size_t data_buffer_size(const void *p); /* 0 if p is not a tracked buffer */
void data_buffer_free(void *p);   /* ignores untracked pointers */

#endif
