/* Undertow - ZAPiT Game Wave emulator (HLE of the ZIT Lua engine).
 * Main thread: SDL window, input, compositing. Game thread: the Lua script. */
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diz.h"
#include "eeprom.h"
#include "input.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "audio.h"
#include "movie.h"
#include "osd.h"
#include "runtime.h"
#include "vfs.h"
#include "zbc.h"
#include "zlibs.h"

typedef struct {
    const char *disc;
    int keys[64], nkeys;
    uint32_t key_start, key_interval;
    uint32_t exit_after;
    const char *screenshot;
    const char *eeprom;
    int scale;
} Options;

static Diz g_diz;
static volatile int g_game_done;
static int g_game_status;

static void usage(void)
{
    fprintf(stderr,
            "usage: undertow <disc.iso | disc_dir> [options]\n"
            "  --trace              log engine API calls\n"
            "  --keys k1,k2,...     scripted key codes (testing)\n"
            "  --key-start MS       time of the first scripted key (default 4000)\n"
            "  --key-interval MS    time between scripted keys (default 1500)\n"
            "  --exit-after MS      quit after MS milliseconds\n"
            "  --screenshot FILE    save a BMP of the screen when quitting via --exit-after\n"
            "  --scale N            window size N*320x240 (default 3)\n"
            "  --eeprom FILE        save-game EEPROM image (default undertow.eep next to the exe)\n"
            "keys (remote 1): arrows, Enter=SELECT, Z/X/C/V=A/B/C/D, 0-9, Backspace=DVD MENU,\n"
            "  Tab=GAME MENU. Game controllers are remotes 2-6: d-pad/left stick,\n"
            "  A/B/X/Y=A/B/C/D, LB/RB=SELECT, Start=GAME MENU, Back=DVD MENU\n");
}

static int parse_args(int argc, char **argv, Options *o)
{
    int i;
    memset(o, 0, sizeof(*o));
    o->key_start = 4000;
    o->key_interval = 1500;
    o->scale = 3;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        int more = i + 1 < argc;
        if (!strcmp(a, "--trace"))
            rt_trace = 1;
        else if (!strcmp(a, "--keys") && more) {
            char *s = argv[++i];
            while (*s && o->nkeys < 64) {
                o->keys[o->nkeys++] = (int)strtol(s, &s, 10);
                if (*s == ',')
                    s++;
            }
        } else if (!strcmp(a, "--key-start") && more)
            o->key_start = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--key-interval") && more)
            o->key_interval = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--exit-after") && more)
            o->exit_after = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(a, "--screenshot") && more)
            o->screenshot = argv[++i];
        else if (!strcmp(a, "--eeprom") && more)
            o->eeprom = argv[++i];
        else if (!strcmp(a, "--scale") && more)
            o->scale = atoi(argv[++i]);
        else if (a[0] != '-' && !o->disc)
            o->disc = a;
        else
            return -1;
    }
    if (o->scale < 1)
        o->scale = 1;
    return o->disc ? 0 : -1;
}

/* ---------------- game thread ---------------- */

static int traceback(lua_State *L)
{
    lua_getglobal(L, "debug");
    lua_pushstring(L, "traceback");
    lua_gettable(L, -2);
    lua_pushvalue(L, 1);
    lua_call(L, 1, 1);
    return 1;
}

static void quit_hook(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    rt_check_quit(L);
}

static int game_thread(void *unused)
{
    unsigned char *buf;
    size_t size;
    lua_State *L;
    int rc;

    (void)unused;
    buf = vfs_read_all(g_diz.appfile, &size);
    if (!buf) {
        fprintf(stderr, "cannot read %s\n", g_diz.appfile);
        g_game_status = 1;
        g_game_done = 1;
        return 1;
    }
    L = lua_open();
    zlibs_open(L);
    lua_sethook(L, quit_hook, LUA_MASKCOUNT, 100000);
    lua_pushcfunction(L, traceback);
    rc = zbc_load(L, buf, size, g_diz.appfile);
    free(buf);
    if (rc != 0) {
        fprintf(stderr, "load error: %s\n", lua_tostring(L, -1));
    } else {
        rc = lua_pcall(L, 0, 0, 1);
        if (rc != 0 && (!rt_quit || rt_trace))   /* on quit, --trace shows where it was */
            fprintf(stderr, "script %s: %s\n", rt_quit ? "stopped" : "error",
                    lua_tostring(L, -1));
        else if (rc == 0)
            printf("script finished\n");
    }
    lua_close(L);
    g_game_status = rc;
    g_game_done = 1;
    return rc;
}

/* ---------------- main thread ---------------- */

static int map_key(SDL_Keycode k)
{
    switch (k) {
    case SDLK_UP: return KEY_UP;
    case SDLK_DOWN: return KEY_DOWN;
    case SDLK_LEFT: return KEY_LEFT;
    case SDLK_RIGHT: return KEY_RIGHT;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: return KEY_SELECT;
    case SDLK_z: return KEY_A;
    case SDLK_x: return KEY_B;
    case SDLK_c: return KEY_C;
    case SDLK_v: return KEY_D;
    case SDLK_BACKSPACE: return KEY_DVD_MENU;
    case SDLK_TAB: return KEY_GAME_MENU;
    default:
        if (k >= SDLK_0 && k <= SDLK_9)
            return KEY_0 + (k - SDLK_0);
        if (k >= SDLK_KP_1 && k <= SDLK_KP_9)
            return 1 + (k - SDLK_KP_1);
        if (k == SDLK_KP_0)
            return 0;
        return -1;
    }
}

/* Game controllers act as remotes 2-6 (the keyboard is remote 1), in connection order. */
#define MAX_PADS 5

typedef struct {
    SDL_GameController *gc;
    SDL_JoystickID id;
    int dir;                /* stick direction currently held, or -1 */
} Pad;

static Pad g_pads[MAX_PADS];

static void pad_add(int index)
{
    int i;
    for (i = 0; i < MAX_PADS && g_pads[i].gc; i++)
        ;
    if (i == MAX_PADS)
        return;
    g_pads[i].gc = SDL_GameControllerOpen(index);
    if (!g_pads[i].gc)
        return;
    g_pads[i].id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pads[i].gc));
    g_pads[i].dir = -1;
    printf("pad \"%s\" is remote %d\n", SDL_GameControllerName(g_pads[i].gc), i + 2);
}

static Pad *pad_find(SDL_JoystickID id, int *remote)
{
    int i;
    for (i = 0; i < MAX_PADS; i++)
        if (g_pads[i].gc && g_pads[i].id == id) {
            *remote = i + 2;
            return &g_pads[i];
        }
    return NULL;
}

static void pad_remove(SDL_JoystickID id)
{
    int remote;
    Pad *p = pad_find(id, &remote);
    if (p) {
        SDL_GameControllerClose(p->gc);
        p->gc = NULL;
        printf("remote %d disconnected\n", remote);
    }
}

static int map_pad_button(int b)
{
    switch (b) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return KEY_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return KEY_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return KEY_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return KEY_RIGHT;
    case SDL_CONTROLLER_BUTTON_A: return KEY_A;
    case SDL_CONTROLLER_BUTTON_B: return KEY_B;
    case SDL_CONTROLLER_BUTTON_X: return KEY_C;
    case SDL_CONTROLLER_BUTTON_Y: return KEY_D;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return KEY_SELECT;
    case SDL_CONTROLLER_BUTTON_START: return KEY_GAME_MENU;
    case SDL_CONTROLLER_BUTTON_BACK: return KEY_DVD_MENU;
    default: return -1;
    }
}

/* Left stick as a d-pad: one key when it leaves the dead zone or changes direction. */
static void pad_stick(Pad *p, int remote)
{
    int x = SDL_GameControllerGetAxis(p->gc, SDL_CONTROLLER_AXIS_LEFTX);
    int y = SDL_GameControllerGetAxis(p->gc, SDL_CONTROLLER_AXIS_LEFTY);
    int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, dir = -1;
    if (ax > 20000 || ay > 20000)
        dir = ax > ay ? (x > 0 ? KEY_RIGHT : KEY_LEFT) : (y > 0 ? KEY_DOWN : KEY_UP);
    else if (ax > 12000 || ay > 12000)
        dir = p->dir;   /* hysteresis */
    if (dir != p->dir && dir >= 0)
        input_push(dir, remote);
    p->dir = dir;
}

static void save_screenshot(SDL_Renderer *r, SDL_Texture *target, const char *path)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 32,
                                                    SDL_PIXELFORMAT_ARGB8888);
    SDL_SetRenderTarget(r, target);
    osd_render(r);
    SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    SDL_SetRenderTarget(r, NULL);
    if (SDL_SaveBMP(s, path) == 0)
        printf("screenshot saved to %s\n", path);
    SDL_FreeSurface(s);
}

int main(int argc, char **argv)
{
    Options opt;
    unsigned char *buf;
    size_t size;
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *target;
    SDL_Thread *thread;
    char title[128];
    int next_key = 0, running = 1;

    if (parse_args(argc, argv, &opt) != 0) {
        usage();
        return 1;
    }
    if (vfs_mount(opt.disc) != 0) {
        fprintf(stderr, "cannot mount %s\n", opt.disc);
        return 1;
    }
    buf = vfs_read_all("gamewave.diz", &size);
    if (!buf) {
        fprintf(stderr, "no gamewave.diz in %s\n", opt.disc);
        return 1;
    }
    buf = realloc(buf, size + 1);
    buf[size] = 0;
    if (diz_parse((const char *)buf, &g_diz) != 0) {
        fprintf(stderr, "bad gamewave.diz\n");
        return 1;
    }
    free(buf);
    snprintf(rt_engine_path, sizeof(rt_engine_path), "%s", g_diz.engine);
    printf("Game: %s (version %s), engine %s board %d (%s)\n", g_diz.appname, g_diz.version,
           g_diz.engine, g_diz.board, g_diz.engine_version);

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    rt_init();
    if (opt.eeprom) {
        eep_open(opt.eeprom);
    } else {   /* one EEPROM per console, shared by every disc as on the real machine */
        char *base = SDL_GetBasePath(), path[1024];
        snprintf(path, sizeof(path), "%sundertow.eep", base ? base : "");
        SDL_free(base);
        eep_open(path);
    }
    if (audio_init() != 0)
        fprintf(stderr, "audio: %s (continuing without sound)\n", SDL_GetError());
    snprintf(title, sizeof(title), "Undertow - %s", g_diz.appname);
    win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           320 * opt.scale, 240 * opt.scale, SDL_WINDOW_RESIZABLE);
    ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED |
                                               SDL_RENDERER_PRESENTVSYNC |
                                               SDL_RENDERER_TARGETTEXTURE)
              : NULL;
    if (!win || !ren) {
        fprintf(stderr, "SDL window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    target = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, SCREEN_W,
                               SCREEN_H);

    thread = SDL_CreateThread(game_thread, "game", NULL);

    while (running) {
        SDL_Event ev;
        uint32_t now = rt_now_ms();
        int ww, wh;
        SDL_Rect dst;

        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                running = 0;
            else if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                int k = map_key(ev.key.keysym.sym);
                if (ev.key.keysym.sym == SDLK_ESCAPE)
                    running = 0;
                else if (k >= 0)
                    input_push(k, 1);
            } else if (ev.type == SDL_CONTROLLERDEVICEADDED) {
                pad_add(ev.cdevice.which);
            } else if (ev.type == SDL_CONTROLLERDEVICEREMOVED) {
                pad_remove(ev.cdevice.which);
            } else if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
                int remote, k = map_pad_button(ev.cbutton.button);
                if (pad_find(ev.cbutton.which, &remote) && k >= 0)
                    input_push(k, remote);
            } else if (ev.type == SDL_CONTROLLERAXISMOTION) {
                int remote;
                Pad *p = pad_find(ev.caxis.which, &remote);
                if (p && (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
                          ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY))
                    pad_stick(p, remote);
            }
        }
        if (next_key < opt.nkeys && now >= opt.key_start + next_key * opt.key_interval)
            input_push(opt.keys[next_key++], 1);
        if (opt.exit_after && now >= opt.exit_after) {
            if (opt.screenshot)
                save_screenshot(ren, target, opt.screenshot);
            running = 0;
        }

        /* compose at native resolution, then scale to a 4:3 area of the window */
        SDL_SetRenderTarget(ren, target);
        osd_render(ren);
        SDL_SetRenderTarget(ren, NULL);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_GetRendererOutputSize(ren, &ww, &wh);
        if (ww * 3 > wh * 4) {
            dst.h = wh;
            dst.w = wh * 4 / 3;
        } else {
            dst.w = ww;
            dst.h = ww * 3 / 4;
        }
        dst.x = (ww - dst.w) / 2;
        dst.y = (wh - dst.h) / 2;
        SDL_RenderCopy(ren, target, NULL, &dst);
        SDL_RenderPresent(ren);
    }

    rt_quit = 1;
    SDL_WaitThread(thread, NULL);
    movie_shutdown();
    audio_shutdown();
    SDL_DestroyTexture(target);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return g_game_done && g_game_status != 0 && !rt_quit ? 2 : 0;
}
