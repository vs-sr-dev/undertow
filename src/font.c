#include "font.h"

#include <stdlib.h>
#include <string.h>

#define HDR 0x280
#define GLYPHS 0x294

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

/* Characters outside [first, first+count) are unchecked in the engine (it reads past its
 * arrays); here they are skipped. */
static const Glyph *glyph(const Font *f, unsigned char c)
{
    int i = (int)c - f->first;
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

/* Layout as in the engine's osd_font.c (0x80608d34): paragraphs split on "\n\r" and words on
 * " \t\n\r" with strtok semantics (runs collapse, empty lines vanish); words are never
 * broken; a word goes on a new line when line + space + word would exceed the width. Within
 * a word each glyph adds kerning + char spacing (arg 9); between words the gap is the advance
 * of glyph ' ' + word spacing (arg 8); lines are cell + line spacing (arg 7) apart. */

typedef struct {
    const char *s;
    int n, line, w;
} Word;

static int is_sep(char c, const char *seps)
{
    return c && strchr(seps, c) != NULL;
}

static int word_width(const Font *f, const char *s, int n, const TextStyle *st)
{
    int i, w = 0;
    for (i = 0; i < n; i++) {
        const Glyph *g = glyph(f, (unsigned char)s[i]);
        if (i > 0)
            w += kern(f, (unsigned char)s[i - 1], (unsigned char)s[i]) + st->char_spacing;
        if (g)
            w += g->advance;
    }
    return w;
}

static int space_width(const Font *f, const TextStyle *st)
{
    const Glyph *g = glyph(f, ' ');
    return (g ? g->advance : 0) + st->word_spacing;
}

/* Returns the word count; *pwords is malloc'ed, *pnlines the line count. */
static int layout(const Font *f, const char *s, const TextStyle *st, int max_width,
                  Word **pwords, int *pnlines)
{
    Word *words = NULL;
    int n = 0, cap = 0, line = -1, line_w = 0, sp = space_width(f, st);

    while (*s) {
        const char *para_end;
        int first_in_para = 1;
        while (is_sep(*s, "\n\r"))
            s++;
        if (!*s)
            break;
        para_end = s;
        while (*para_end && !is_sep(*para_end, "\n\r"))
            para_end++;
        while (s < para_end) {
            const char *e;
            int ww;
            while (s < para_end && is_sep(*s, " \t"))
                s++;
            if (s >= para_end)
                break;
            e = s;
            while (e < para_end && !is_sep(*e, " \t"))
                e++;
            ww = word_width(f, s, (int)(e - s), st);
            if (first_in_para || (max_width > 0 && line_w + sp + ww > max_width)) {
                line++;
                line_w = ww;
            } else {
                line_w += sp + ww;
            }
            first_in_para = 0;
            if (n == cap) {
                cap = cap ? cap * 2 : 64;
                words = realloc(words, (size_t)cap * sizeof(*words));
            }
            words[n++] = (Word){s, (int)(e - s), line, ww};
            s = e;
        }
        s = para_end;
    }
    *pwords = words;
    *pnlines = line + 1;
    return n;
}

static void line_widths(const Word *words, int n, int nlines, int sp, int *lw)
{
    int i;
    for (i = 0; i < nlines; i++)
        lw[i] = 0;
    for (i = 0; i < n; i++)
        lw[words[i].line] += (i > 0 && words[i - 1].line == words[i].line ? sp : 0) + words[i].w;
}

void font_measure(const Font *f, const char *s, const TextStyle *st, int max_width, int *w,
                  int *h)
{
    Word *words;
    int nl, n, i, mw = 0, *lw;

    if (st->simple) {   /* RenderSimple: every byte is a glyph, one line */
        int len = (int)strlen(s);
        mw = 0;
        for (i = 0; i < len; i++) {
            const Glyph *g = glyph(f, (unsigned char)s[i]);
            mw += g ? g->advance : 0;
        }
        *w = mw;
        *h = f->cell;
        return;
    }
    n = layout(f, s, st, max_width, &words, &nl);
    lw = malloc(((size_t)nl + 1) * sizeof(int));
    line_widths(words, n, nl, space_width(f, st), lw);
    for (i = 0; i < nl; i++)
        if (lw[i] > mw)
            mw = lw[i];
    *w = mw;
    *h = nl ? nl * f->cell + (nl - 1) * st->line_spacing : 0;
    free(lw);
    free(words);
}

/* Glyph = atlas columns [x0+lb, x0+lb+ink) x rows [y0, y1), drawn at pen x + lb. */
static void blit_glyph(const Font *f, const Glyph *g, uint8_t *dst, int w, int h, int px,
                       int py, const TextStyle *st)
{
    int x, y, gh = g->y1 - g->y0;
    for (y = 0; y < gh; y++) {
        int sy = g->y0 + y, dy = py + y;
        if (dy < 0 || dy >= h || sy < 0 || sy >= f->ah)
            continue;
        for (x = 0; x < g->width; x++) {
            int sx = g->x0 + g->lsb + x, dx = px + g->lsb + x;
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

static int draw_word(const Font *f, const char *s, int n, const TextStyle *st, uint8_t *rgba,
                     int w, int h, int x, int y, int kerning)
{
    int i;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        const Glyph *g = glyph(f, c);
        if (i > 0 && kerning)
            x += kern(f, (unsigned char)s[i - 1], c) + st->char_spacing;
        if (!g)
            continue;
        blit_glyph(f, g, rgba, w, h, x, y, st);
        x += g->lsb + g->width + g->rsb;
    }
    return x;
}

void font_render(const Font *f, const char *s, const TextStyle *st, uint8_t *rgba, int w,
                 int h)
{
    Word *words;
    int nl, n, i, total, y, x = 0, sp, *lw;

    if (st->simple) {
        draw_word(f, s, (int)strlen(s), st, rgba, w, h, 0, 0, 0);
        return;
    }
    sp = space_width(f, st);
    n = layout(f, s, st, w, &words, &nl);
    lw = malloc(((size_t)nl + 1) * sizeof(int));
    line_widths(words, n, nl, sp, lw);
    total = nl ? nl * f->cell + (nl - 1) * st->line_spacing : 0;
    y = st->valign == 1 ? (h - total) / 2 : st->valign == 2 ? h - total : 0;
    for (i = 0; i < n; i++) {
        int ln = words[i].line;
        if (i == 0 || words[i - 1].line != ln) {
            x = st->halign == 1 ? (w - lw[ln]) / 2 : st->halign == 2 ? w - lw[ln] : 0;
            if (i > 0)
                y += (ln - words[i - 1].line) * (f->cell + st->line_spacing);
        } else {
            x += sp;
        }
        x = draw_word(f, words[i].s, words[i].n, st, rgba, w, h, x, y, 1);
    }
    free(lw);
    free(words);
}
