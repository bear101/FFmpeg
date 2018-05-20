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
#include <libavutil/intreadwrite.h>
#include <string.h>

/*
 * Parser implemented using the following document:
 *
 * EUROCONTROL STANDARD DOCUMENT FOR SURVEILLANCE DATA EXCHANGE
 * Category 240 Radar Video Transmission
 *
 * Edition : 1.1
 * Edition Date : May 2009
 * Status : Released Issue
 * Class : General Public  */

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
    uint8_t cat240[5];
    uint16_t len, fspec;

    /* read Video Data Block */
    if (avio_read(pb, cat240, 5) != 5)
        return AVERROR(EIO);

    len = AV_RB16(&cat240[1]);
    fspec = AV_RB16(&cat240[3]); /* 1 or 2 octets */

    /* look for 240 */
    if (cat240[0] != 0xf0)
        return AVERROR(EIO);
    
    // jump to next cat240
    avio_seek(pb, len, SEEK_SET);

    if (avio_read(pb, cat240, 5) != 5)
        return AVERROR(EIO);

    /* look for 240 */
    if (cat240[0] != 0xf0)
        return AVERROR(EIO);

    /* rewind */
    avio_seek(pb, 0, SEEK_SET);

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->width = 4096;
    st->codecpar->height = 2048;
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
    AVIOContext *pb = s->pb;
    uint8_t cat240[8], messagetype;
    uint16_t len, fspec, datasource;
    int ret;

    /* read Video Data Block */
    while ((ret = avio_read(pb, cat240, sizeof(cat240))) > 0) {

        if (ret != sizeof(cat240) || cat240[0] != 0xf0)
            return AVERROR(EIO);

        /* We except Standard UAP format with FSPEC size 2 */
        len = AV_RB16(&cat240[1]);
        fspec = AV_RB16(&cat240[3]);
        datasource = AV_RB16(&cat240[5]);
        messagetype = cat240[7];

        /* rewind to beginning of message */
        if (avio_seek(pb, -1 * sizeof(cat240), SEEK_CUR) < 0)
            return AVERROR(EIO);

        /* must be Video Message 002 (Video Summary is 001) */
        if (messagetype == 002) {
            if (av_get_packet(s->pb, pkt, len) != len || cc > 5000)
                return AVERROR_EOF;

            pkt->stream_index = 0;
            pkt->pts = cc;
            pkt->dts = cc++;
            pkt->duration = 1;

            s->streams[pkt->stream_index]->duration++;
            return len;
        }

        if (avio_seek(pb, len, SEEK_CUR) < 0)
            return AVERROR(EIO);
    }
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
