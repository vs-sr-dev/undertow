#include "movie.h"

#include <stdio.h>
#include <string.h>

#include <SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>

#include "audio.h"
#include "osd.h"
#include "runtime.h"
#include "vfs.h"

#define IO_BUF 65536

static char g_loaded[512];          /* path given to Load, started by Play */
static char g_path[512];            /* path the player thread is playing */
static SDL_Thread *g_thread;
static volatile int g_stop, g_playing, g_loop;

/* ---- custom I/O over the VFS ---- */

static int io_read(void *opaque, uint8_t *buf, int size)
{
    size_t n = vfs_read(opaque, buf, (size_t)size);
    return n ? (int)n : AVERROR_EOF;
}

static int64_t io_seek(void *opaque, int64_t off, int whence)
{
    VfsFile *f = opaque;
    uint64_t pos;
    switch (whence & ~AVSEEK_FORCE) {
    case AVSEEK_SIZE: return (int64_t)vfs_size(f);
    case SEEK_SET: pos = (uint64_t)off; break;
    case SEEK_END: pos = vfs_size(f) + off; break;
    default: return -1;   /* SEEK_CUR is resolved by avio itself */
    }
    return vfs_seek(f, pos) == 0 ? (int64_t)pos : -1;
}

/* wait until wall-clock time `due` (SDL ticks), returning early on stop */
static void wait_until(uint32_t due)
{
    for (;;) {
        int32_t left = (int32_t)(due - SDL_GetTicks());
        if (left <= 0 || g_stop || rt_quit)
            return;
        SDL_Delay(left > 5 ? 5 : (Uint32)left);
    }
}

static int player(void *unused)
{
    VfsFile *file = vfs_open(g_path);
    AVFormatContext *fmt = NULL;
    AVIOContext *io = NULL;
    AVCodecContext *vdec = NULL, *adec = NULL;
    SwrContext *swr = NULL;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int vs = -1, as = -1;
    int64_t base_ms = AV_NOPTS_VALUE;
    uint32_t start = 0;
    int16_t *pcm = NULL;
    int pcm_cap = 0;

    (void)unused;
    if (!file) {
        fprintf(stderr, "movie: cannot open %s\n", g_path);
        goto done;
    }
    fmt = avformat_alloc_context();
    io = avio_alloc_context(av_malloc(IO_BUF), IO_BUF, 0, file, io_read, NULL, io_seek);
    fmt->pb = io;
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&fmt, NULL, NULL, NULL) < 0 ||
        avformat_find_stream_info(fmt, NULL) < 0) {
        fprintf(stderr, "movie: cannot parse %s\n", g_path);
        goto done;
    }
    vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    as = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (vs >= 0) {
        const AVCodec *c = avcodec_find_decoder(fmt->streams[vs]->codecpar->codec_id);
        vdec = avcodec_alloc_context3(c);
        avcodec_parameters_to_context(vdec, fmt->streams[vs]->codecpar);
        if (avcodec_open2(vdec, c, NULL) < 0)
            avcodec_free_context(&vdec);
    }
    if (as >= 0) {
        const AVCodec *c = avcodec_find_decoder(fmt->streams[as]->codecpar->codec_id);
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        adec = avcodec_alloc_context3(c);
        avcodec_parameters_to_context(adec, fmt->streams[as]->codecpar);
        if (avcodec_open2(adec, c, NULL) < 0 ||
            swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_S16, AUDIO_RATE, &adec->ch_layout,
                                adec->sample_fmt, adec->sample_rate, 0, NULL) < 0 ||
            swr_init(swr) < 0)
            avcodec_free_context(&adec);
    }

    while (!g_stop && !rt_quit) {
        int ret = av_read_frame(fmt, pkt);
        if (ret < 0) {
            if (!g_loop)
                break;
            /* loop: rewind, keep the clock continuous */
            av_seek_frame(fmt, -1, 0, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_BYTE);
            if (vdec)
                avcodec_flush_buffers(vdec);
            if (adec)
                avcodec_flush_buffers(adec);
            base_ms = AV_NOPTS_VALUE;
            continue;
        }
        if (pkt->stream_index == vs && vdec && avcodec_send_packet(vdec, pkt) >= 0) {
            AVRational tb = fmt->streams[vs]->time_base;
            while (avcodec_receive_frame(vdec, frame) == 0) {
                int64_t ts = frame->best_effort_timestamp;
                if (ts != AV_NOPTS_VALUE) {
                    int64_t ms = av_rescale_q(ts, tb, (AVRational){1, 1000});
                    if (base_ms == AV_NOPTS_VALUE) {
                        base_ms = ms;
                        start = SDL_GetTicks();
                    }
                    wait_until(start + (uint32_t)(ms - base_ms));
                }
                if (g_stop || rt_quit)
                    break;
                if (frame->format == AV_PIX_FMT_YUV420P)
                    osd_video_set(frame->data[0], frame->linesize[0], frame->data[1],
                                  frame->data[2], frame->linesize[1], frame->width,
                                  frame->height);
            }
        } else if (pkt->stream_index == as && adec && avcodec_send_packet(adec, pkt) >= 0) {
            while (avcodec_receive_frame(adec, frame) == 0) {
                int out = swr_get_out_samples(swr, frame->nb_samples);
                if (out > pcm_cap) {
                    pcm_cap = out;
                    pcm = av_realloc(pcm, (size_t)pcm_cap * 4);
                }
                out = swr_convert(swr, (uint8_t **)&pcm, pcm_cap,
                                  (const uint8_t **)frame->extended_data, frame->nb_samples);
                if (out > 0)
                    audio_stream_write(pcm, out, &g_stop);
            }
        }
        av_packet_unref(pkt);
    }

done:
    av_free(pcm);
    swr_free(&swr);
    avcodec_free_context(&vdec);
    avcodec_free_context(&adec);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    if (fmt)
        avformat_close_input(&fmt);
    if (io) {
        av_freep(&io->buffer);
        avio_context_free(&io);
    }
    vfs_close(file);
    g_playing = 0;
    return 0;
}

static void join(void)
{
    if (g_thread) {
        g_stop = 1;
        SDL_WaitThread(g_thread, NULL);
        g_thread = NULL;
    }
    g_stop = 0;
}

int movie_load(const char *disc_path)
{
    VfsFile *f = vfs_open(disc_path);
    if (!f) {
        fprintf(stderr, "movie.Load: missing %s\n", disc_path);
        return -1;
    }
    vfs_close(f);
    snprintf(g_loaded, sizeof(g_loaded), "%s", disc_path);
    return 0;
}

void movie_play(void)
{
    join();
    audio_stream_clear();
    if (!g_loaded[0])
        return;
    memcpy(g_path, g_loaded, sizeof(g_path));
    g_playing = 1;
    g_thread = SDL_CreateThread(player, "movie", NULL);
}

void movie_stop(void)
{
    join();
    audio_stream_clear();
    g_playing = 0;
}

void movie_set_loop(int loop)
{
    g_loop = loop;
}

int movie_playing(void)
{
    return g_playing;
}

void movie_shutdown(void)
{
    join();
}
