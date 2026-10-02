/* MPEG-2 decoding (FFmpeg): still "iframes" now, movies later. */
#ifndef UNDERTOW_VIDEO_H
#define UNDERTOW_VIDEO_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int w, h;
    uint8_t *y, *u, *v;   /* tightly packed planes, chroma (w+1)/2 x (h+1)/2 */
} YuvImage;

/* Decode the first picture of an MPEG-1/2 elementary stream. Returns 0 on success. */
int video_decode_still(const uint8_t *data, size_t size, YuvImage *out);
void video_free_image(YuvImage *img);

#endif
