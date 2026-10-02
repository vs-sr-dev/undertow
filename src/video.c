#include "video.h"

#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>

static int copy_frame(const AVFrame *f, YuvImage *out)
{
    int row, cw = (f->width + 1) / 2, ch = (f->height + 1) / 2;
    if (f->format != AV_PIX_FMT_YUV420P)
        return -1;
    out->w = f->width;
    out->h = f->height;
    out->y = malloc((size_t)f->width * f->height);
    out->u = malloc((size_t)cw * ch);
    out->v = malloc((size_t)cw * ch);
    if (!out->y || !out->u || !out->v)
        return -1;
    for (row = 0; row < f->height; row++)
        memcpy(out->y + (size_t)row * f->width, f->data[0] + (size_t)row * f->linesize[0],
               f->width);
    for (row = 0; row < ch; row++) {
        memcpy(out->u + (size_t)row * cw, f->data[1] + (size_t)row * f->linesize[1], cw);
        memcpy(out->v + (size_t)row * cw, f->data[2] + (size_t)row * f->linesize[2], cw);
    }
    return 0;
}

int video_decode_still(const uint8_t *data, size_t size, YuvImage *out)
{
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_MPEG2VIDEO);
    AVCodecParserContext *parser = NULL;
    AVCodecContext *ctx = NULL;
    AVPacket *pkt = NULL;
    AVFrame *frame = NULL;
    int rc = -1;

    memset(out, 0, sizeof(*out));
    if (!codec || !(parser = av_parser_init(codec->id)) ||
        !(ctx = avcodec_alloc_context3(codec)) || avcodec_open2(ctx, codec, NULL) < 0 ||
        !(pkt = av_packet_alloc()) || !(frame = av_frame_alloc()))
        goto done;

    while (size > 0 && rc != 0) {
        int used = av_parser_parse2(parser, ctx, &pkt->data, &pkt->size, data, (int)size,
                                    AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (used < 0)
            break;
        data += used;
        size -= (size_t)used;
        if (pkt->size && avcodec_send_packet(ctx, pkt) >= 0)
            while (rc != 0 && avcodec_receive_frame(ctx, frame) == 0)
                rc = copy_frame(frame, out);
    }
    if (rc != 0) {
        /* a lone I-frame is only output once parser and decoder are flushed */
        av_parser_parse2(parser, ctx, &pkt->data, &pkt->size, NULL, 0, AV_NOPTS_VALUE,
                         AV_NOPTS_VALUE, 0);
        if (pkt->size)
            avcodec_send_packet(ctx, pkt);
        avcodec_send_packet(ctx, NULL);
        while (rc != 0 && avcodec_receive_frame(ctx, frame) == 0)
            rc = copy_frame(frame, out);
    }
done:
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    if (parser)
        av_parser_close(parser);
    if (rc != 0)
        video_free_image(out);
    return rc;
}

void video_free_image(YuvImage *img)
{
    free(img->y);
    free(img->u);
    free(img->v);
    memset(img, 0, sizeof(*img));
}
