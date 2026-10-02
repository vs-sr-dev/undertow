/* Undertow - ZAPiT Game Wave emulator (HLE of the ZIT Lua engine).
 * Main thread: SDL window, input, compositing. Game thread: the Lua script. */
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diz.h"
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
            "keys: arrows, Enter=SELECT, Z/X/C/V=A/B/C/D, 0-9, Backspace=DVD MENU, Tab=GAME MENU\n");
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
        if (rc != 0 && !rt_quit)
            fprintf(stderr, "script error: %s\n", lua_tostring(L, -1));
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
    printf("Game: %s (version %s), engine %s board %d (%s)\n", g_diz.appname, g_diz.version,
           g_diz.engine, g_diz.board, g_diz.engine_version);

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    rt_init();
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
