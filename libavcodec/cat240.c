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

#include "avcodec.h"
#include "internal.h"
#include <libavutil/rational.h>

static av_cold int cat240_decode_init(AVCodecContext *avctx)
{
    av_log(avctx, AV_LOG_INFO, "init\n");
    av_log(avctx, AV_LOG_INFO, "Pixel fmt %d\n", avctx->pix_fmt);
    av_log(avctx, AV_LOG_INFO, "Framerate %g\n", av_q2d(avctx->framerate));
    av_log(avctx, AV_LOG_INFO, "Timebase %g\n", av_q2d(avctx->time_base));

    return 0;
}

static int cat240_decode_frame(AVCodecContext *avctx,
                               void *data, int *got_frame,
                               AVPacket *avpkt)
{
    int ret, x, y;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    AVFrame* frame = av_frame_alloc();
    uint8_t *framedata;
    int framesize;
    
    if ((ret = ff_reget_buffer(avctx, frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to alloc frame buffer\n");
        return ret;
    }

    framedata = frame->data[0];
    framesize = frame->height * frame->linesize[0];

    memset(framedata, 0, framesize);
    for (int i=0;i<framesize;i+=4) {
        int j = (frame->pkt_pts % 90) / 30;
        framedata[i+j] = 0xff;
        framedata[i+3] = 0x00;
    }

    frame->key_frame = 1;

    if ((ret = av_frame_ref(data, frame)) < 0)
        return ret;

    *got_frame = 1;
    return framesize;
}

AVCodec ff_cat240_decoder = {
    .name           = "cat240",
    .long_name      = NULL_IF_CONFIG_SMALL("CAT240 Radar Video (Eurocontrol Category 240)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_CAT240,
    .init           = cat240_decode_init,
    .decode         = cat240_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
