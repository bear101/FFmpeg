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

#include "asterix.h"
#include <libavutil/rational.h>
#include <string.h>

/* const AVCodecTag ff_codec_asterix_tags[] = { */
/*     { AV_CODEC_ID_CAT240,            0 }, */
/*     { AV_CODEC_ID_NONE,               0 }, */
/* }; */

static int asterix_probe(AVProbeData *p)
{
    if (p->filename && strstr(p->filename, ".asterix"))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int asterix_read_header(AVFormatContext *s)
{
    AVStream *st;
    AVIOContext *pb = s->pb;
    AVRational fps = {1, 30};

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->width = 1024;
    st->codecpar->height = 1024;
    st->codecpar->codec_id = AV_CODEC_ID_CAT240;
    st->codecpar->format = AV_PIX_FMT_RGB32;
    st->time_base = fps;
    st->start_time = 0;
    st->duration = 0;
    av_log(s, AV_LOG_INFO, "opening asterix file. Stream index %d\n", st->index);

    return 0;
}

int cc = 0;

static int asterix_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;

    ret = av_get_packet(s->pb, pkt, 0x1000);
    if (ret != 0x1000)
        ret = AVERROR_EOF;
    pkt->stream_index = 0;
    pkt->pts = cc;
    pkt->dts = cc++;
    pkt->duration = 1;

    s->streams[pkt->stream_index]->duration++;
    
    return ret;
}

AVInputFormat ff_asterix_demuxer = {
    .name           = "asterix",
    .long_name      = NULL_IF_CONFIG_SMALL("ASTERIX Radar Video (Eurocontrol Category 240)"),
    .read_probe     = asterix_probe,
    .read_header    = asterix_read_header,
    .read_packet    = asterix_read_packet,
    .extensions     = "asterix",
    .flags          = AVFMT_GENERIC_INDEX,
    /* .codec_tag      = (const AVCodecTag* const []){ff_codec_asterix_tags, 0}, */
};
