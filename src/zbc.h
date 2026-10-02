#ifndef UNDERTOW_ZBC_H
#define UNDERTOW_ZBC_H

#include <stddef.h>

#include "lua.h"

/* Load a .zbc (packed or unpacked) as a Lua chunk; pushes the function or an error
 * message, returning 0 or a lua_load error code. */
int zbc_load(lua_State *L, const unsigned char *data, size_t size, const char *name);

#endif
