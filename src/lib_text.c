/* Lua libraries: font, text. A text object is a texture with the rendered string plus an
 * (initially hidden) overlay; scripts position it via text.GetOverlayId + gl.SetParameters.
 *
 * text.Render(str, font, w, h [, halign, valign, line_spacing, char_spacing, ?, tint,
 *             Y, Cb, Cr, ?]) - see docs/NOTES.md (spacings from apps/zit/osd_font.c);
 * colours are YCbCr (128,128 = neutral chroma). text.RenderSimple(font, str) sizes the
 * texture to the string. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "lauxlib.h"
#include "lualib.h"
#include "osd.h"
#include "runtime.h"
#include "vfs.h"
#include "zbm.h"
#include "zlibs.h"

#define MAX_FONTS 32
#define MAX_TEXTS 512

typedef struct {
    int id;
    Font *font;
} FontSlot;

typedef struct {
    int id, tex, ovl;
} TextObj;

static FontSlot g_fonts[MAX_FONTS];
static TextObj g_texts[MAX_TEXTS];
static int g_builtin_font;

extern void rm_join_path(char *out, size_t size, int res, const char *name);

static int add_font(Font *f)
{
    int i;
    for (i = 0; i < MAX_FONTS; i++) {
        if (!g_fonts[i].id) {
            g_fonts[i].id = rt_new_id();
            g_fonts[i].font = f;
            return g_fonts[i].id;
        }
    }
    font_free(f);
    return -1;
}

static Font *find_font(int id)
{
    int i;
    for (i = 0; i < MAX_FONTS; i++)
        if (g_fonts[i].id == id && id)
            return g_fonts[i].font;
    return NULL;
}

static Font *make_font(const uint8_t *dat, size_t datsize, const uint8_t *zbm, size_t zbmsize)
{
    int w, h;
    uint8_t *atlas = zbm_decode(zbm, zbmsize, &w, &h);
    Font *f = atlas ? font_create(dat, datsize, atlas, w, h) : NULL;
    if (!f)
        free(atlas);
    return f;
}

static int fn_load(lua_State *L)
{
    char path[512], tpath[512];
    const char *name = luaL_checkstring(L, 2), *atlas_name;
    int res = luaL_checkint(L, 1);
    size_t ds = 0, zs = 0;
    uint8_t *dat, *zbm = NULL;
    Font *f = NULL;

    rm_join_path(path, sizeof(path), res, name);
    dat = vfs_read_all(path, &ds);
    atlas_name = dat ? font_atlas_name(dat, ds) : NULL;
    if (atlas_name) {
        rm_join_path(tpath, sizeof(tpath), res, atlas_name);
        zbm = vfs_read_all(tpath, &zs);
    }
    if (zbm)
        f = make_font(dat, ds, zbm, zs);
    free(dat);
    free(zbm);
    if (!f) {
        fprintf(stderr, "font.Load: cannot load %s\n", path);
        lua_pushnumber(L, -1);
        return 1;
    }
    lua_pushnumber(L, add_font(f));
    return 1;
}

static int fn_free(lua_State *L)
{
    int i, id = luaL_checkint(L, 1);
    for (i = 0; i < MAX_FONTS; i++) {
        if (g_fonts[i].id == id && id) {
            font_free(g_fonts[i].font);
            g_fonts[i].id = 0;
            if (id == g_builtin_font)
                g_builtin_font = 0;
        }
    }
    return 0;
}

/* The engine binary on the disc carries a built-in font in its file table. */
static const uint8_t *cheese_find(const uint8_t *d, size_t n, const char *name, size_t *size)
{
    static const uint8_t magic[8] = {0x12, 0x34, 0x56, 0x78, 0x87, 0x65, 0x43, 0x21};
    size_t base, p;
    uint32_t i, count;
    for (base = 0; base + 12 <= n; base += 4)
        if (!memcmp(d + base, magic, 8))
            break;
    if (base + 12 > n)
        return NULL;
    count = (d[base + 8] << 24) | (d[base + 9] << 16) | (d[base + 10] << 8) | d[base + 11];
    for (i = 0, p = base + 12; i < count && p + 48 <= n; i++, p += 48) {
        uint32_t off = (d[p + 40] << 24) | (d[p + 41] << 16) | (d[p + 42] << 8) | d[p + 43];
        uint32_t len = (d[p + 44] << 24) | (d[p + 45] << 16) | (d[p + 46] << 8) | d[p + 47];
        if (!strncmp((const char *)d + p, name, 40) && base + off + len <= n) {
            *size = len;
            return d + base + off;
        }
    }
    return NULL;
}

static int fn_getbuiltin(lua_State *L)
{
    if (!g_builtin_font) {
        size_t n, ds, zs;
        uint8_t *eng = vfs_read_all(rt_engine_path, &n);
        const uint8_t *dat = eng ? cheese_find(eng, n, "f_1955_s_22_m_4633_32_128.dat", &ds) : NULL;
        const uint8_t *zbm = eng ? cheese_find(eng, n, "f_1955_s_22_m_4633_32_128.zbm", &zs) : NULL;
        Font *f = dat && zbm ? make_font(dat, ds, zbm, zs) : NULL;
        free(eng);
        if (f)
            g_builtin_font = add_font(f);
        else
            fprintf(stderr, "font: no built-in font in %s\n", rt_engine_path);
    }
    lua_pushnumber(L, g_builtin_font ? g_builtin_font : -1);
    return 1;
}

static const luaL_reg font_lib[] = {
    {"Load", fn_load}, {"Free", fn_free}, {"GetBuiltinFontID", fn_getbuiltin}, {NULL, NULL}};

/* ---- text ---- */

static uint8_t clamp8(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v;
}

static int new_text(lua_State *L, const Font *f, const char *s, const TextStyle *st, int w,
                    int h)
{
    uint8_t *rgba;
    int i;
    if (w <= 0 || h <= 0)
        font_measure(f, s, st, 0, &w, &h);
    if (w <= 0)
        w = 1;
    if (h <= 0)
        h = 1;
    rgba = calloc((size_t)w * h, 4);
    font_render(f, s, st, rgba, w, h);
    for (i = 0; i < MAX_TEXTS; i++) {
        if (!g_texts[i].id) {
            g_texts[i].tex = osd_texture_add(rgba, w, h);
            g_texts[i].ovl = osd_overlay_create(g_texts[i].tex);
            g_texts[i].id = rt_new_id();
            lua_pushnumber(L, g_texts[i].id);
            return 1;
        }
    }
    free(rgba);
    lua_pushnumber(L, -1);
    return 1;
}

static int tx_rendersimple(lua_State *L)
{
    const Font *f = find_font(luaL_checkint(L, 1));
    const char *s = lua_tostring(L, 2);
    TextStyle st = {0};
    rt_trace_call(L, "text.RenderSimple");
    if (!f || !s) {
        lua_pushnumber(L, -1);
        return 1;
    }
    return new_text(L, f, s, &st, 0, 0);
}

static int tx_render(lua_State *L)
{
    const char *s = lua_tostring(L, 1);
    const Font *f = find_font(luaL_checkint(L, 2));
    int w = luaL_checkint(L, 3), h = luaL_checkint(L, 4);
    TextStyle st;

    rt_trace_call(L, "text.Render");
    if (!f || !s) {
        lua_pushnumber(L, -1);
        return 1;
    }
    st.halign = luaL_optint(L, 5, 0);
    st.valign = luaL_optint(L, 6, 0);
    st.line_spacing = (signed char)luaL_optint(L, 7, 0);   /* engine adds it to the line height */
    st.char_spacing = (signed char)luaL_optint(L, 8, 0);   /* and this to each glyph advance */
    st.tint = luaL_optint(L, 10, 0) != 0;
    {
        int y = luaL_optint(L, 11, 235), cb = luaL_optint(L, 12, 128) - 128,
            cr = luaL_optint(L, 13, 128) - 128;
        st.r = clamp8(y + (45 * cr) / 32);
        st.g = clamp8(y - (11 * cb + 23 * cr) / 32);
        st.b = clamp8(y + (113 * cb) / 64);
    }
    return new_text(L, f, s, &st, w, h);
}

static TextObj *find_text(int id)
{
    int i;
    for (i = 0; i < MAX_TEXTS; i++)
        if (g_texts[i].id == id && id)
            return &g_texts[i];
    return NULL;
}

static int tx_remove(lua_State *L)
{
    TextObj *t = find_text(luaL_checkint(L, 1));
    if (t) {
        osd_overlay_free(t->ovl);
        osd_texture_free(t->tex);
        t->id = 0;
    }
    return 0;
}

static int tx_getoverlayid(lua_State *L)
{
    TextObj *t = find_text(luaL_checkint(L, 1));
    lua_pushnumber(L, t ? t->ovl : -1);
    return 1;
}

static const luaL_reg text_lib[] = {
    {"RenderSimple", tx_rendersimple}, {"Render", tx_render}, {"Remove", tx_remove},
    {"GetOverlayId", tx_getoverlayid}, {NULL, NULL}};

void libtext_open(lua_State *L)
{
    luaL_openlib(L, "font", font_lib, 0);
    luaL_openlib(L, "text", text_lib, 0);
    lua_settop(L, 0);
}
