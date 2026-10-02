/* .zbm OSD textures -> RGBA8888 (bytes R,G,B,A). Two variants are known:
 *  - game discs: 0x30-byte header (version 1), u16 BE pixels with pairs swapped, A4 Y6 Cb3 Cr3
 *  - firmware:   0x2C-byte header (first word 0x10), u16 LE pixels, A4 Y4 Cb4 Cr4 */
#ifndef UNDERTOW_ZBM_H
#define UNDERTOW_ZBM_H

#include <stddef.h>
#include <stdint.h>

/* Returns malloc'd RGBA pixels or NULL. */
uint8_t *zbm_decode(const uint8_t *data, size_t size, int *w, int *h);

#endif
