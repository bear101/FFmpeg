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

typedef struct AsterixContext {
    uint32_t start_tod; /* 24-bit */
    int64_t last_pts;
} AsterixContext;

static int asterix_probe(AVProbeData *p)
{
    if (p->filename && strstr(p->filename, ".asterix"))
        return AVPROBE_SCORE_MAX;

    av_log(p, AV_LOG_INFO, "opening asterix file %s\n", p->filename);
    return 0;
}

/* static int asterix_open(URLContext *h, const char *uri, int flags) */
/* { */
/*     av_log(p, AV_LOG_INFO, "asterix url open %s\n", uri); */
/*     return 0; */
/* } */

static int asterix_read_header(AVFormatContext *s)
{
    AsterixContext* ctx = s->priv_data;
    AVStream *st;
    AVIOContext *pb = s->pb;
    AVRational fps = {1, 30};
    uint8_t cat240[5];
    uint16_t len, fspec;

    ctx->start_tod = -1;
    ctx->last_pts = 0;

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

static int asterix_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AsterixContext* ctx = s->priv_data;
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

            double elapsed;
            
            /* process Time of Day (len = 3) */
            char todbuf[4];
            uint32_t tod = 0;
            if (avio_seek(pb, len - 3, SEEK_CUR) > 0 &&
                avio_read(pb, todbuf, 3) > 0) {
                tod = AV_RB24(todbuf);
                av_log(s, AV_LOG_DEBUG, "Time of Day: %u\n", (unsigned)tod);
            }

            if (ctx->start_tod == -1)
                ctx->start_tod = tod;

            if (avio_seek(pb, -len, SEEK_CUR) < 0)
                return AVERROR(EIO);
            
            if (av_get_packet(s->pb, pkt, len) != len)
                return AVERROR_EOF;

            pkt->stream_index = 0;

            elapsed = (tod - ctx->start_tod) / 128.;
            
            av_log(s, AV_LOG_DEBUG, "Duration: %g, Framerate %g\n",
                   elapsed, av_q2d(s->streams[pkt->stream_index]->time_base));

            pkt->duration = 0;
            pkt->pts = ctx->last_pts;
            pkt->dts = ctx->last_pts;
            while (elapsed >= (ctx->last_pts + 1) * av_q2d(s->streams[pkt->stream_index]->time_base)) {
                pkt->pts = ++ctx->last_pts;
                ++pkt->duration;
            }
            
            av_log(s, AV_LOG_DEBUG, "PTS %d, DTS %d\n", (int)pkt->pts, (int)pkt->dts);

            s->streams[pkt->stream_index]->duration++;
            return len;
        }

        if (avio_seek(pb, len, SEEK_CUR) < 0)
            return AVERROR(EIO);
    }
    return ret;
}

static int asterix_read_close(AVFormatContext *s)
{
    return 0;
}

AVInputFormat ff_asterix_demuxer = {
    .name           = "asterix",
    .long_name      = NULL_IF_CONFIG_SMALL("ASTERIX Radar Video (Eurocontrol Category 240)"),
    .read_probe     = asterix_probe,
    /* .url_open       = asterix_open, */
    .priv_data_size = sizeof(AsterixContext),
    .read_header    = asterix_read_header,
    .read_packet    = asterix_read_packet,
    .read_close     = asterix_read_close,
    .extensions     = "asterix",
    .flags          = AVFMT_GENERIC_INDEX,
    /* .codec_tag      = (const AVCodecTag* const []){ff_codec_asterix_tags, 0}, */
};
