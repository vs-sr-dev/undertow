cmake_minimum_required(VERSION 3.16)
project(undertow C)

set(CMAKE_C_STANDARD 99)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# --- Lua 5.0.2 (patched, integer lua_Number; see third_party/lua-5.0.2/UNDERTOW_PATCHES.md)
set(LUA_DIR ${CMAKE_SOURCE_DIR}/third_party/lua-5.0.2)
add_library(lua50 STATIC
  ${LUA_DIR}/src/lapi.c ${LUA_DIR}/src/lcode.c ${LUA_DIR}/src/ldebug.c ${LUA_DIR}/src/ldo.c
  ${LUA_DIR}/src/ldump.c ${LUA_DIR}/src/lfunc.c ${LUA_DIR}/src/lgc.c ${LUA_DIR}/src/llex.c
  ${LUA_DIR}/src/lmem.c ${LUA_DIR}/src/lobject.c ${LUA_DIR}/src/lopcodes.c
  ${LUA_DIR}/src/lparser.c ${LUA_DIR}/src/lstate.c ${LUA_DIR}/src/lstring.c
  ${LUA_DIR}/src/ltable.c ${LUA_DIR}/src/ltm.c ${LUA_DIR}/src/lundump.c ${LUA_DIR}/src/lvm.c
  ${LUA_DIR}/src/lzio.c
  ${LUA_DIR}/src/lib/lauxlib.c ${LUA_DIR}/src/lib/lbaselib.c ${LUA_DIR}/src/lib/ldblib.c
  ${LUA_DIR}/src/lib/lstrlib.c ${LUA_DIR}/src/lib/ltablib.c)
target_include_directories(lua50 PUBLIC ${LUA_DIR}/include ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(lua50 PUBLIC "LUA_USER_H=\"luauser.h\"")
target_compile_options(lua50 PRIVATE -w)

find_package(ZLIB REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswresample libavutil)

add_executable(undertow
  src/main.c src/runtime.c src/vfs.c src/diz.c src/zbc.c src/zbm.c src/osd.c src/video.c
  src/input.c src/audio.c src/movie.c src/zlibs.c src/lib_gl.c src/lib_sys.c src/lib_media.c
  src/font.c src/lib_text.c src/lib_data.c)
target_include_directories(undertow PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_directories(undertow PRIVATE ${SDL2_LIBRARY_DIRS})
# SDL_MAIN_HANDLED: plain console main(), no SDL2main / -mwindows
target_link_libraries(undertow PRIVATE lua50 ZLIB::ZLIB PkgConfig::FFMPEG SDL2)
target_compile_options(undertow PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation)
