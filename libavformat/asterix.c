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
#include "avio_internal.h"

#include <libavutil/rational.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/opt.h>
#include <libavcodec/cat240.h>
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
    int frame_rate;
} AsterixContext;

static int asterix_probe(AVProbeData *p)
{
    if (p->filename && strstr(p->filename, ".asterix")) {
        return AVPROBE_SCORE_MAX;
    }

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
    uint8_t cat240[5], *msg_buf = 0;
    Cat240VideoMessage msg;
    uint16_t len;
    int ret;

    memset(&msg, 0, sizeof(msg));

    ctx->start_tod = -1;
    ctx->last_pts = 0;

    if (ctx->frame_rate <= 0) {
        ctx->frame_rate = fps.den;
    }

    while (msg.nb_cells == 0) {

        if (avio_read(pb, cat240, sizeof(cat240)) != sizeof(cat240))
            goto error;

        if (avio_seek(pb, -sizeof(cat240), SEEK_CUR) < 0)
            goto error;

        /* look for 240 */
        if (cat240[0] != 0xf0)
            goto error;

        len = AV_RB16(&cat240[1]);

        if ((ret = ffio_ensure_seekback(pb, len)) < 0) {
            av_log(s, AV_LOG_ERROR, "Failed to enable seek back when parsing header. Error: %d\n", ret);
            return ret;
        }

        msg_buf = av_malloc(len);
        if (avio_read(pb, msg_buf, len) != len)
            goto error;

        if (parse_cat240_videomessage(s, msg_buf, len, &msg) < 0)
            goto error;

        av_free(msg_buf);
    }

    /* rewind */
    avio_seek(pb, 0, SEEK_SET);

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->width = st->codecpar->height = msg.nb_cells * 2;
    st->codecpar->codec_id = AV_CODEC_ID_CAT240;
    st->codecpar->format = AV_PIX_FMT_RGB32;
    st->time_base = fps;
    st->start_time = 0;
    st->duration = 0;
    av_log(s, AV_LOG_INFO, "opening asterix file. Stream index %d\n", st->index);

    return 0;

error:
    av_free(msg_buf);
    return AVERROR(EIO);
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

        if (ret != sizeof(cat240) || cat240[0] != 0xf0) {
            av_log(s, AV_LOG_ERROR, "Separator 0xf0 not found at %"PRId64"\n", avio_tell(pb));
            return AVERROR(EIO);
        }

        /* We except Standard UAP format with FSPEC size 2 */
        len = AV_RB16(&cat240[1]);
        fspec = AV_RB16(&cat240[3]);
        datasource = AV_RB16(&cat240[5]);
        messagetype = cat240[7];

        av_log(s, AV_LOG_DEBUG, "Field Specification: 0x%"PRIx16"Data Source Identifier 0x%"PRIx16"\n",
               fspec, datasource);

        if ((ret = ffio_ensure_seekback(pb, len)) < 0) {
            av_log(s, AV_LOG_ERROR, "Failed to enable seek back at %"PRId64". Error: %d\n",
                   avio_tell(pb), ret);
        }

        /* rewind to beginning of message */
        if ((ret = avio_seek(pb, -1 * sizeof(cat240), SEEK_CUR)) < 0) {
            av_log(s, AV_LOG_ERROR, "Unable to rewind to header after reading message type at %"PRId64". Error: %d\n",
                   avio_tell(pb), ret);
            return ret;
        }

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

            if ((ret = avio_seek(pb, -len, SEEK_CUR)) < 0) {
                av_log(s, AV_LOG_ERROR, "Unable to rewind %"PRIu16" bytes to header after reading timestamp at %"PRId64". Error: %d. Now skipping %"PRIu16" bytes\n", len, avio_tell(pb), ret, len);
                return ret;
            }

            if ((ret = av_get_packet(s->pb, pkt, len)) != len) {
                av_log(s, AV_LOG_ERROR, "Unable to read full packet from %"PRId64". Error: %d\n", avio_tell(pb), ret);
                return ret;
            }

            pkt->stream_index = 0;

            elapsed = (tod - ctx->start_tod) / 128.;
            /* elapsed *= 10.; */

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

        if ((ret = avio_skip(pb, len)) < 0) {
            av_log(s, AV_LOG_ERROR, "Unable to rewind to header start from %"PRId64"\n", avio_tell(pb));
            return ret;
        }
    }

    return ret;
}

static int asterix_read_close(AVFormatContext *s)
{
    return 0;
}

#define OFFSET(x) offsetof(AsterixContext, x)
static const AVOption demux_options[] = {
    { .name   = "frame_rate",
      .help   = "Frame rate",
      .offset = OFFSET(frame_rate),
      .type   = AV_OPT_TYPE_INT,
      { .i64  = -1 },
      .min    = -1,
      .max    = INT_MAX,
      .flags  = AV_OPT_FLAG_DECODING_PARAM,
      .unit   = NULL },
    { NULL },
};

static const AVClass asterix_demuxer_class = {
    .class_name = "Asterix demuxer",
    .item_name  = av_default_item_name,
    .option     = demux_options,
    .version    = LIBAVUTIL_VERSION_INT
};

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
    /* .priv_class     = &asterix_demuxer_class, */
};
