/* Remote-control key queue (filled by the main thread, read by the game). */
#ifndef UNDERTOW_INPUT_H
#define UNDERTOW_INPUT_H

#include <stdint.h>

/* key codes as used by the game scripts */
enum {
    KEY_0 = 0, KEY_UP = 10, KEY_DOWN = 11, KEY_RIGHT = 12, KEY_LEFT = 13, KEY_SELECT = 14,
    KEY_DVD_MENU = 15, KEY_A = 16, KEY_B = 17, KEY_C = 18, KEY_D = 19, KEY_GAME_MENU = 20,
    KEY_NONE = 255
};

void input_push(int key, int remote);
int input_pop(int *key, int *remote, uint32_t *timestamp);   /* 1 if a key was available */
void input_clear(void);

#endif
