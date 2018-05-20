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
#include <libavutil/avassert.h>
#include <libavutil/intreadwrite.h>

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

    /* it's a CAT240 */
    av_assert0(buf[0] == 0xf0);

    /* process Data Source Identifier (len = 2) */
    uint16_t datasource;
    datasource = AV_RB16(&buf[5]);
    av_log(avctx, AV_LOG_DEBUG, "System Area Code (SAC): 0x%x, System Identification Code (SIC): 0x%x\n",
           (datasource >> 8), (datasource & 0xff));
    
    /* process Message Type (len = 1) */
    av_assert0(buf[7] == 002);

    /* process Video Record Header (len = 4) */
    uint32_t msgseqid;
    msgseqid = AV_RB32(&buf[8]);
    av_log(avctx, AV_LOG_DEBUG, "Message Sequence Identifier: %u\n", msgseqid);
    
    /* process Video Header Nano (len = 12) or Video Header Femto (len = 12) */
    uint16_t start_az, end_az;
    uint32_t start_rg, cell_dur;
    start_az = AV_RB16(&buf[12]);
    end_az = AV_RB16(&buf[14]);
    start_rg = AV_RB32(&buf[16]);
    cell_dur = AV_RB32(&buf[20]);
    av_log(avctx, AV_LOG_DEBUG, "START_AZ: %u, END_AZ: %u, START_RG: %u, CELL_DUR: %u\n",
           (unsigned)start_az, (unsigned)end_az, (unsigned)start_rg, (unsigned)cell_dur);

    /* process Video Cells Resolution & Data Compression Indicator (len = 2) */
    uint16_t vcr_dci;
    uint8_t res;
    vcr_dci = AV_RB16(&buf[24]);
    res = (vcr_dci & 0xff);
    av_log(avctx, AV_LOG_DEBUG, "Data Compression: %s, Spare: 0x%x, RES: %u\n",
           (vcr_dci & 0x8000)?"true":"false", (vcr_dci >> 8) & 0x7f, (unsigned)res);

    /* process Video Octets & Video Cells Counters (len = 5) */
    uint16_t nb_vb;
    uint32_t nb_cells;
    nb_vb = AV_RB16(&buf[26]);
    nb_cells = AV_RB24(&buf[28]);
    av_log(avctx, AV_LOG_DEBUG, "NB_VB: %u, NB_CELLS: %u\n", (unsigned)nb_vb, (unsigned)nb_cells);
    
    /* process Video Block Low/Medium/High Volume */
    uint8_t rep;
    uint16_t video_block;
    res = buf[31];
    video_block = AV_RB16(&buf[32]);
    switch (res) {
    case 1 : /* Monobit Resolution */ break;
    case 2 : /* Low Resolution */ break;
    case 3 : /* Medium Resolution */ break;
    case 4 : /* High Resolution */
        break;
    case 5 : /* Very High Resolution */ break;
    case 6 : /* Ultra High Resolution */ break;
    }

    /* process Time of Day (len = 3) */

    
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
