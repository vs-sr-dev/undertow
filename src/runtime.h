/* Shared runtime state between the Lua (game) thread and the main (SDL) thread. */
#ifndef UNDERTOW_RUNTIME_H
#define UNDERTOW_RUNTIME_H

#include <stdint.h>

#include <SDL.h>

#include "lua.h"

#define SCREEN_W 720
#define SCREEN_H 480

extern SDL_mutex *rt_lock;     /* guards OSD, video plane, input queue */
extern volatile int rt_quit;   /* set by the main thread to stop the game thread */
extern int rt_trace;           /* --trace: log engine API calls */

void rt_init(void);
uint32_t rt_now_ms(void);      /* engine clock (ms since start) */
int rt_new_id(void);           /* engine object handles share one counter */

/* Raise a Lua error if the emulator is shutting down (call from blocking API calls). */
void rt_check_quit(lua_State *L);

/* Sleep the game thread, waking early on quit. */
void rt_sleep(lua_State *L, uint32_t ms);

/* Log an API call with its arguments when tracing. */
void rt_trace_call(lua_State *L, const char *name);

#endif
