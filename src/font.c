#include "font.h"

#include <stdlib.h>
#include <string.h>

#define HDR 0x280
#define GLYPHS 0x294
#define MAX_LINES 64

typedef struct {
    int x0, y0, x1, y1, advance, lsb, width, rsb;
} Glyph;

typedef struct {
    int first, second, adjust;
} Kern;

struct Font {
    int first, count, cell;
    Glyph *glyphs;
    Kern *kerns;
    int nkerns;
    uint8_t *atlas;
    int aw, ah;
};

static int rd32(const uint8_t *p)
{
    return (int)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24));
}

const char *font_atlas_name(const uint8_t *dat, size_t size)
{
    if (size < GLYPHS || !memchr(dat + 0x200, 0, 0x80))
        return NULL;
    return (const char *)dat + 0x200;
}

Font *font_create(const uint8_t *dat, size_t size, uint8_t *atlas, int aw, int ah)
{
    Font *f;
    int i, count, nkerns;
    size_t need;

    if (size < GLYPHS)
        return NULL;
    count = rd32(dat + HDR);
    nkerns = rd32(dat + HDR + 16);
    need = GLYPHS + (size_t)count * 32 + (size_t)nkerns * 12;
    if (count <= 0 || count > 256 || nkerns < 0 || need > size)
        return NULL;
    f = calloc(1, sizeof(*f));
    f->first = rd32(dat + HDR + 4);
    f->count = count;
    f->cell = rd32(dat + HDR + 12);
    f->glyphs = calloc((size_t)count, sizeof(Glyph));
    for (i = 0; i < count; i++) {
        const uint8_t *g = dat + GLYPHS + i * 32;
        f->glyphs[i] = (Glyph){rd32(g), rd32(g + 4), rd32(g + 8), rd32(g + 12),
                               rd32(g + 16), rd32(g + 20), rd32(g + 24), rd32(g + 28)};
    }
    f->nkerns = nkerns;
    f->kerns = calloc((size_t)nkerns + 1, sizeof(Kern));
    for (i = 0; i < nkerns; i++) {
        const uint8_t *k = dat + GLYPHS + count * 32 + i * 12;
        f->kerns[i] = (Kern){rd32(k), rd32(k + 4), rd32(k + 8)};
    }
    f->atlas = atlas;
    f->aw = aw;
    f->ah = ah;
    return f;
}

void font_free(Font *f)
{
    if (!f)
        return;
    free(f->glyphs);
    free(f->kerns);
    free(f->atlas);
    free(f);
}

int font_line_height(const Font *f)
{
    return f->cell;
}

static const Glyph *glyph(const Font *f, unsigned char c)
{
    int i = (int)c - f->first;
    if (i < 0 || i >= f->count)
        i = '?' - f->first;
    return i >= 0 && i < f->count ? &f->glyphs[i] : NULL;
}

static int kern(const Font *f, unsigned char a, unsigned char b)
{
    int i;
    for (i = 0; i < f->nkerns; i++)
        if (f->kerns[i].first == a && f->kerns[i].second == b)
            return f->kerns[i].adjust;
    return 0;
}

static int span_width(const Font *f, const char *s, int n, const TextStyle *st)
{
    int i, w = 0;
    for (i = 0; i < n; i++) {
        const Glyph *g = glyph(f, (unsigned char)s[i]);
        if (g)
            w += g->advance + st->char_spacing;
        if (i + 1 < n)
            w += kern(f, (unsigned char)s[i], (unsigned char)s[i + 1]);
    }
    return n ? w - st->char_spacing : 0;
}

typedef struct {
    const char *s;
    int n;
} Line;

/* Split on '\n' and wrap at spaces to max_width. */
static int layout(const Font *f, const char *s, const TextStyle *st, int max_width,
                  Line *lines)
{
    int nl = 0;
    while (nl < MAX_LINES) {
        const char *end = strchr(s, '\n');
        int n = end ? (int)(end - s) : (int)strlen(s);
        while (max_width > 0 && span_width(f, s, n, st) > max_width) {
            int cut = n;
            while (cut > 0 && s[cut] != ' ')
                cut--;
            while (cut > 0 && span_width(f, s, cut, st) > max_width) {
                cut--;
                while (cut > 0 && s[cut] != ' ')
                    cut--;
            }
            if (cut <= 0 || nl >= MAX_LINES - 1)
                break;
            lines[nl++] = (Line){s, cut};
            s += cut + 1;
            n -= cut + 1;
        }
        lines[nl++] = (Line){s, n};
        if (!end)
            break;
        s = end + 1;
    }
    return nl;
}

void font_measure(const Font *f, const char *s, const TextStyle *st, int max_width, int *w,
                  int *h)
{
    Line lines[MAX_LINES];
    int i, nl = layout(f, s, st, max_width, lines), mw = 0;
    for (i = 0; i < nl; i++) {
        int lw = span_width(f, lines[i].s, lines[i].n, st);
        if (lw > mw)
            mw = lw;
    }
    *w = mw;
    *h = nl * f->cell + (nl - 1) * st->line_spacing;
}

static void blit_glyph(const Font *f, const Glyph *g, uint8_t *dst, int w, int h, int px,
                       int py, const TextStyle *st)
{
    int x, y, gw = g->advance, gh = g->y1 - g->y0;
    for (y = 0; y < gh; y++) {
        int sy = g->y0 + y, dy = py + y;
        if (dy < 0 || dy >= h || sy < 0 || sy >= f->ah)
            continue;
        for (x = 0; x < gw; x++) {
            int sx = g->x0 + x, dx = px + x;
            const uint8_t *sp;
            uint8_t *dp;
            if (dx < 0 || dx >= w || sx < 0 || sx >= f->aw)
                continue;
            sp = f->atlas + ((size_t)sy * f->aw + sx) * 4;
            dp = dst + ((size_t)dy * w + dx) * 4;
            if (sp[3] <= dp[3])
                continue;    /* keep the most opaque pixel where glyphs overlap */
            if (st->tint) {
                dp[0] = st->r;
                dp[1] = st->g;
                dp[2] = st->b;
            } else {
                dp[0] = sp[0];
                dp[1] = sp[1];
                dp[2] = sp[2];
            }
            dp[3] = sp[3];
        }
    }
}

void font_render(const Font *f, const char *s, const TextStyle *st, uint8_t *rgba, int w,
                 int h)
{
    Line lines[MAX_LINES];
    int nl = layout(f, s, st, w, lines), i, total, y;

    total = nl * f->cell + (nl - 1) * st->line_spacing;
    y = st->valign == 1 ? (h - total) / 2 : st->valign == 2 ? h - total : 0;
    for (i = 0; i < nl; i++, y += f->cell + st->line_spacing) {
        int j, lw = span_width(f, lines[i].s, lines[i].n, st);
        int x = st->halign == 1 ? (w - lw) / 2 : st->halign == 2 ? w - lw : 0;
        for (j = 0; j < lines[i].n; j++) {
            unsigned char c = (unsigned char)lines[i].s[j];
            const Glyph *g = glyph(f, c);
            if (!g)
                continue;
            blit_glyph(f, g, rgba, w, h, x, y, st);
            x += g->advance + st->char_spacing;
            if (j + 1 < lines[i].n)
                x += kern(f, c, (unsigned char)lines[i].s[j + 1]);
        }
    }
}
