#include "input.h"

#include <stdio.h>

#include "runtime.h"

#define QUEUE 32

typedef struct {
    int key, remote;
    uint32_t time;
} KeyEvent;

static KeyEvent g_q[QUEUE];
static int g_head, g_count;

void input_push(int key, int remote)
{
    SDL_LockMutex(rt_lock);
    if (g_count < QUEUE) {
        KeyEvent *e = &g_q[(g_head + g_count++) % QUEUE];
        e->key = key;
        e->remote = remote;
        e->time = rt_now_ms();
    }
    SDL_UnlockMutex(rt_lock);
    if (rt_trace)
        printf("[%7u] <key %d remote %d>\n", (unsigned)rt_now_ms(), key, remote);
}

int input_pop(int *key, int *remote, uint32_t *timestamp)
{
    int got = 0;
    SDL_LockMutex(rt_lock);
    if (g_count) {
        KeyEvent *e = &g_q[g_head];
        *key = e->key;
        *remote = e->remote;
        *timestamp = e->time;
        g_head = (g_head + 1) % QUEUE;
        g_count--;
        got = 1;
    }
    SDL_UnlockMutex(rt_lock);
    return got;
}

void input_clear(void)
{
    SDL_LockMutex(rt_lock);
    g_head = g_count = 0;
    SDL_UnlockMutex(rt_lock);
}
