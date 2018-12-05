/*
 * ASTERIX Radar Video Transmission (Category 240) demuxer
 *
 * Copyright (c) 2018 Bjørn Damstedt Rasmussen
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "cat240.h"
#include "avcodec.h"
#include "internal.h"
#include <libavutil/rational.h>
#include <libavutil/avassert.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/opt.h>

#if CONFIG_ZLIB
#include <zlib.h>
#endif
#include <math.h>

#define ASTERIX_AZIMUTH_RESOLUTION 0x10000

typedef struct Cat240Context {
    AVClass *class;
    uint8_t decompress_buf[0x10000]; /* TODO: Should be dynamically
                                      * allocated based on Video Block
                                      * Low/Medium/High Data Volume,
                                      * i.e. 1020, 16320 and 65024
                                      * bytes */
    AVFrame *frame;
    int keyframe_az; /* key frame is one full scan (start_az in
                      * Cat240VideoMessage-struct */
    int scans; /* Frames are not submitted until a scan has
                * completed. Normally the frame rate setting
                * determines when frames are submitted. */
    int square; /* Draw in top half of square instead of a circle */
} Cat240Context;


int parse_cat240_videomessage(void *avcl, const uint8_t* buf,
                              uint16_t size, Cat240VideoMessage* msg)
{
    const uint8_t* buf_ptr = buf;

    /* it's a CAT240 */
    if (*buf_ptr != 0xf0)
        return AVERROR(EIO);

    buf_ptr += 1;

    msg->len = AV_RB16(buf_ptr);
    buf_ptr += 2;

    msg->fspec = AV_RB16(buf_ptr);
    buf_ptr += 2;
    av_log(avcl, AV_LOG_DEBUG, "LEN: %u, FSPEC: %x\n",
           (unsigned)msg->len, (unsigned)msg->fspec);

    /* process Data Source Identifier (len = 2) */
    msg->datasource = AV_RB16(buf_ptr);
    buf_ptr += 2;
    av_log(avcl, AV_LOG_DEBUG, "System Area Code (SAC): 0x%x, System Identification Code (SIC): 0x%x\n",
           (msg->datasource >> 8), (msg->datasource & 0xff));

    /* process Message Type (len = 1) */
    if (*buf_ptr != VIDEOSUMMARY_MSGTYPE)
        return 1; /* we only want Video Message type */

    buf_ptr += 1;

    /* process Video Record Header (len = 4) */
    msg->msgseqid = AV_RB32(buf_ptr);
    buf_ptr += 4;
    av_log(avcl, AV_LOG_DEBUG, "Message Sequence Identifier: %u\n", msg->msgseqid);

    /* process Video Header Nano (len = 12) or Video Header Femto (len = 12) */
    msg->start_az = AV_RB16(buf_ptr);
    buf_ptr += 2;
    msg->end_az = AV_RB16(buf_ptr);
    buf_ptr += 2;
    msg->start_rg = AV_RB32(buf_ptr);
    buf_ptr += 4;
    msg->cell_dur = AV_RB32(buf_ptr);
    buf_ptr += 4;
    av_log(avcl, AV_LOG_DEBUG, "START_AZ: %u, END_AZ: %u, START_RG: %u, CELL_DUR: %u\n",
           (unsigned)msg->start_az, (unsigned)msg->end_az,
           (unsigned)msg->start_rg, (unsigned)msg->cell_dur);

    /* process Video Cells Resolution & Data Compression Indicator (len = 2) */
    msg->vcr_dci = AV_RB16(buf_ptr);
    buf_ptr += 2;
    msg->res = (msg->vcr_dci & 0xff);
    av_log(avcl, AV_LOG_DEBUG, "Data Compression: %s, Spare: 0x%x, RES: %u\n",
           (msg->vcr_dci & 0x8000)?"true":"false", (msg->vcr_dci >> 8) & 0x7f,
           (unsigned)msg->res);

    /* process Video Block Low/Medium/High Volume */
    switch (msg->res) {
    case 1 : /* Monobit Resolution */ break;
    case 2 : /* Low Resolution */ break;
    case 3 : /* Medium Resolution */ break;
    case 4 : /* High Resolution */ break;
    case 5 : /* Very High Resolution */ break;
    case 6 : /* Ultra High Resolution */ break;
    }

    /* process Video Octets & Video Cells Counters (len = 5) */
    msg->nb_vb = AV_RB16(buf_ptr);
    buf_ptr += 2;
    msg->nb_cells = AV_RB24(buf_ptr);
    buf_ptr += 3;
    av_log(avcl, AV_LOG_DEBUG, "NB_VB: %u, NB_CELLS: %u\n",
           (unsigned)msg->nb_vb, (unsigned)msg->nb_cells);

    /* process Video Block Low/Medium/High Volume */
    msg->rep = *buf_ptr;
    buf_ptr += 1;

    msg->data = buf_ptr;

    /* ensure video data is not bigger than the buffer */
    if (buf_ptr + msg->nb_vb > buf + size - 3)
        return AVERROR_INVALIDDATA;

    /* process Time of Day (len = 3) */
    buf_ptr = buf + size - 3;
    msg->tod = AV_RB24(buf_ptr);
    av_log(avcl, AV_LOG_DEBUG, "Time of Day: %u\n", (unsigned)msg->tod);

    return 0;
};

static av_cold int cat240_decode_init(AVCodecContext *avctx)
{
    Cat240Context *ctx = avctx->priv_data;
    ctx->frame = av_frame_alloc();
    ctx->keyframe_az = -1;
    
    if (!ctx->frame)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int cat240_decode_end(AVCodecContext *avctx)
{
    Cat240Context *ctx = avctx->priv_data;
    av_frame_free(&ctx->frame);
    return 0;
}

#if CONFIG_ZLIB
static int decompress_videoblocks(AVCodecContext *avctx, const uint8_t* buf, int size)
{
    int ret;
    z_stream strm = {Z_NULL};
    Cat240Context *ctx = avctx->priv_data;

    ret = inflateInit(&strm);
    if (ret != Z_OK)
        return AVERROR(ENOMEM);

    strm.avail_in = size;
    strm.total_in = size;
    strm.next_in = buf;
    strm.avail_out = sizeof(ctx->decompress_buf);
    strm.next_out = ctx->decompress_buf;

    ret = inflate(&strm, Z_FINISH);

    inflateEnd(&strm);

    return ret == Z_STREAM_END ? sizeof(ctx->decompress_buf) - strm.avail_out : AVERROR_INVALIDDATA;
}
#endif

static int cat240_draw_slice(AVCodecContext *avctx, uint16_t start_az,
                             uint16_t end_az, const uint8_t* sweep_data, int sweep_len)
{
    Cat240Context *ctx = avctx->priv_data;
    uint8_t *framedata = ctx->frame->data[0];
    uint8_t *center = framedata + ((ctx->frame->height / 2) * ctx->frame->linesize[0]) + (ctx->frame->linesize[0] / 2);
    double angle;
    int w = (end_az - start_az + ASTERIX_AZIMUTH_RESOLUTION) % ASTERIX_AZIMUTH_RESOLUTION;

    while (--w >= 0) {
        int r;
        double a = ((start_az + w) % ASTERIX_AZIMUTH_RESOLUTION) / (double)ASTERIX_AZIMUTH_RESOLUTION, s, c;
        angle = M_PI * 2. * -a + M_PI;
        s = sin(angle);
        c = cos(angle);

        /* int x_max = s * avctx->width/2; */
        /* int y_max = c * avctx->width/2; */

        /* av_log(avctx, AV_LOG_DEBUG, "Angle: %g, Deg: %g, max x=%d,y=%d\n", angle, 360.0 * a, x_max, y_max); */

        r = avctx->width/2;
        r = r > sweep_len? sweep_len : r;
        while (--r >= 0) {
            int x = s * r;
            int y = c * r;
            uint8_t *ptr = center + (ctx->frame->linesize[0] * y);
            ptr += x * 4;

            av_assert0(ptr >= framedata);
            av_assert0(ptr < framedata + ctx->frame->height * ctx->frame->linesize[0]);
            ptr[0] = 0;
            ptr[1] = sweep_data[r];
            ptr[2] = 0;
            ptr[3] = 0;
        }
    }

    return 0;
}

static int cat240_decode_frame(AVCodecContext *avctx,
                               void *data, int *got_frame,
                               AVPacket *avpkt)
{
    int ret, x, y;
    Cat240Context *ctx = avctx->priv_data;
    const uint8_t *video_uncompressed;
    uint8_t *framedata;
    int framesize;
    int range;
    Cat240VideoMessage msg;

    ret = parse_cat240_videomessage(avctx, avpkt->data, avpkt->size, &msg);
    if (ret < 0)
        return ret;

    if (msg.vcr_dci & 0x8000) {
#if CONFIG_ZLIB
        range = decompress_videoblocks(avctx, msg.data, msg.nb_vb);
        video_uncompressed = ctx->decompress_buf;
#else
        av_log(avctx, AV_LOG_ERROR, "zlib compressed video received but not enabled in FFmpeg compilation\n");
        return AVERROR_INVALIDDATA;
#endif
    } else {
        range = msg.nb_vb;
        video_uncompressed = msg.data;
    }
    
    av_log(avctx, AV_LOG_DEBUG, "Range: %u\n", (unsigned)range);

    if (range < 0)
        return range;

    if ((ret = ff_reget_buffer(avctx, ctx->frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to alloc frame buffer\n");
        return ret;
    }

    framedata = ctx->frame->data[0];
    framesize = ctx->frame->height * ctx->frame->linesize[0];

    if (!ctx->square) {
        cat240_draw_slice(avctx, msg.start_az, msg.end_az, video_uncompressed, msg.nb_cells);
    } else {
        int range_max = range > avctx->height / 2? avctx->height / 2 : range;
        x = msg.start_az / (ASTERIX_AZIMUTH_RESOLUTION / avctx->width);
        for (y = 0;y < range_max; y++) {
            int pos = ctx->frame->linesize[0] * y + x * 4;
            framedata[pos] = 0;
            framedata[pos+1] = video_uncompressed[y];
            framedata[pos+2] = 0;
            framedata[pos+3] = 0;
        }
    }

    /* mark as key frame if a full scan has completed */
    ctx->frame->key_frame = ctx->keyframe_az == (int)msg.start_az;

    if (ctx->keyframe_az == -1) {
        ctx->keyframe_az = msg.start_az;
    }

    /* Don't forward to decoder if time is unchanged and we're not
     * waiting for a complete scan */
    if (avpkt->pts == avpkt->dts && !ctx->scans)
        return framesize;

    /* Process a full scan */
    if (ctx->scans && !ctx->frame->key_frame) {
        return framesize;
    }

    if ((ret = av_frame_ref(data, ctx->frame)) < 0) {
        return ret;
    }
    
    *got_frame = 1;

    return framesize;
}

static const AVOption decoder_options[] = {
    { .name   = "scan",
      .help   = "Submit frame when scan completes. Default is to submit frames based on FPS setting.",
      .offset = offsetof(Cat240Context, scans),
      .type   = AV_OPT_TYPE_BOOL,
      { .i64  = 0 },
      .min    = 0,
      .max    = 1,
      .flags  = AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM,
      .unit   = NULL },
    
    { .name   = "square",
      .help   = "Draw square instead of circle",
      .offset = offsetof(Cat240Context, square),
      .type   = AV_OPT_TYPE_BOOL,
      { .i64  = 0 },
      .min    = 0,
      .max    = 1,
      .flags  = AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM,
      .unit   = NULL },
    
    { NULL },
};

static const AVClass cat240_decoder_class = {
    .class_name = "CAT240 decoder",
    .item_name  = av_default_item_name,
    .option     = decoder_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_cat240_decoder = {
    .name           = "cat240",
    .long_name      = NULL_IF_CONFIG_SMALL("CAT240 Radar Video (Eurocontrol Category 240)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_CAT240,
    .priv_data_size = sizeof(Cat240Context),
    .init           = cat240_decode_init,
    .decode         = cat240_decode_frame,
    .close          = cat240_decode_end,
    .capabilities   = AV_CODEC_CAP_DR1,
    .priv_class     = &cat240_decoder_class,
};
