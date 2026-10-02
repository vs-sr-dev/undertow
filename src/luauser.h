/* Lua 5.0.2 configuration matching the ZIT engine: lua_Number is a 32-bit int
 * (bytecode header: sizeof(lua_Number) == 4, test number 31415926). */
#ifndef UNDERTOW_LUAUSER_H
#define UNDERTOW_LUAUSER_H

#include <stdlib.h>

#define LUA_NUMBER          int
#define LUA_NUMBER_SCAN     "%d"
#define LUA_NUMBER_FMT      "%d"
#define lua_str2number(s,p) ((int)strtol((s), (p), 10))

#endif
