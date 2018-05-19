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

const AVCodecTag ff_codec_asterix_tags[] = {
    { AV_CODEC_ID_ASTERIX,            0 },
    { AV_CODEC_ID_NONE,               0 },
};

static int asterix_probe(AVProbeData *p)
{
    return AVPROBE_SCORE_MAX;
}

static int asterix_read_header(AVFormatContext *s)
{
    AVStream *st;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->width         = 1024;
    st->codecpar->height        = 1024;
    st->codecpar->codec_id   = AV_CODEC_ID_ASTERIX;

    

    return 0;
}

static int asterix_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;

    ret = av_get_packet(s->pb, pkt, s->streams[0]->codecpar->width *
                        s->streams[0]->codecpar->height * 4);
    pkt->pos++;

    if (pkt->pos == 1000)
        ret = AVERROR_EOF;
    
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
    .codec_tag      = (const AVCodecTag* const []){ff_codec_asterix_tags, 0},
};
