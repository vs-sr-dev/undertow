/* Bitmap fonts: a .dat metrics file plus a .zbm atlas (16x6 cells, ASCII 32..127).
 *
 * .dat layout (little-endian):
 *   0x000 char[128] face name, 0x080 char[128] family, 0x100 char[128] charset ("Western"),
 *   0x180 char[128] style, 0x200 char[128] atlas file name (.zbm)
 *   0x280 int count, first_char, end_char, cell_size, kerning_count
 *   0x294 count x {int x0, y0, x1, y1, advance, left_bearing, ink_width, right_bearing}
 *   then kerning_count x {int first, second, adjust}
 * A glyph is atlas columns [x0+lb, x0+lb+ink) x rows [y0, y1), drawn at pen x + lb; the pen
 * then moves by lb + ink + rb, while measuring uses `advance`. */
#ifndef UNDERTOW_FONT_H
#define UNDERTOW_FONT_H

#include <stddef.h>
#include <stdint.h>

typedef struct Font Font;

typedef struct {
    int halign;           /* 0 left, 1 center, 2 right */
    int valign;           /* 0 top, 1 center, 2 bottom */
    int char_spacing;     /* extra pixels between glyphs of a word (text.Render arg 9) */
    int word_spacing;     /* extra pixels between words (arg 8) */
    int line_spacing;     /* extra pixels between lines (arg 7) */
    int simple;           /* RenderSimple: every byte drawn as a glyph on one line */
    int tint;             /* replace glyph colour with r,g,b */
    uint8_t r, g, b;
} TextStyle;

/* atlas file name stored in a .dat (NULL if malformed) */
const char *font_atlas_name(const uint8_t *dat, size_t size);

/* takes ownership of atlas_rgba */
Font *font_create(const uint8_t *dat, size_t size, uint8_t *atlas_rgba, int aw, int ah);
void font_free(Font *f);
int font_line_height(const Font *f);

/* Size of the text after wrapping to max_width (0 = no wrapping). */
void font_measure(const Font *f, const char *s, const TextStyle *st, int max_width, int *w,
                  int *h);

/* Render into an RGBA buffer of w x h. */
void font_render(const Font *f, const char *s, const TextStyle *st, uint8_t *rgba, int w,
                 int h);

#endif
