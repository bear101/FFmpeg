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

static int asterix_decode_frame(AVCodecContext *avctx,
                                void *data, int *got_frame,
                                AVPacket *avpkt)
{
    int ret;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;

    ret = ff_set_dimensions(avctx, w, h);
    avctx->pix_fmt = AV_PIX_FMT_ARGB;
}

AVCodec ff_asterix_decoder = {
    .name           = "asterix",
    .long_name      = NULL_IF_CONFIG_SMALL("ASTERIX Radar Video (Eurocontrol Category 240)"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_ASTERIX,
    .decode         = asterix_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
