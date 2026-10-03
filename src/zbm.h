/* .zbm OSD textures -> RGBA8888 (bytes R,G,B,A). Two variants are known:
 *  - game discs: 0x30-byte header (version 1), u16 BE pixels with pairs swapped, in one of
 *    the engine's pixel formats (header +8; 0 = the current OSD mode):
 *    1 "844" Y8 Cb4 Cr4, 2 "655" Y6 Cb5 Cr5, 3 "4444" A4 Y4 Cb4 Cr4, 4 "4633" A4 Y6 Cb3 Cr3
 *    (bit layouts from the engine's colour packer 0x80612860). Every disc image is 4633.
 *  - firmware:   0x2C-byte header (first word 0x10), u16 LE pixels, A4 Y4 Cb4 Cr4 */
#ifndef UNDERTOW_ZBM_H
#define UNDERTOW_ZBM_H

#include <stddef.h>
#include <stdint.h>

/* Returns malloc'd RGBA pixels or NULL. */
uint8_t *zbm_decode(const uint8_t *data, size_t size, int *w, int *h);

/* Engine format code for a mode name ("844" 1, "655" 2, "4444" 3, "4633" 4; else 0), as
 * 0x80612780 parses gl.SelectOSDMode / gl.CreateEmptyTexture arguments. */
int zbm_format_code(const char *name);

/* gl.SelectOSDMode: the format that format-0 textures take (default 4 "4633"). */
void zbm_set_osd_mode(int fmt);

#endif
