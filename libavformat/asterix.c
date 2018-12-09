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
#include <libavutil/imgutils.h>
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
    AVClass *class;
    uint32_t start_tod; /* 24-bit */
    int64_t last_pts;
    int frame_rate;
} AsterixContext;

static int asterix_probe(AVProbeData *p)
{
    if (p->filename && strstr(p->filename, ".asterix")) {
        return AVPROBE_SCORE_MAX;
    }

    return 0;
}

static int asterix_read_header(AVFormatContext *s)
{
    AsterixContext* ctx = s->priv_data;
    AVStream *st;
    AVIOContext *pb = s->pb;
    AVRational fps = {1, 30};
    uint8_t cat240[5], *msg_buf = 0;
    Cat240VideoMessage msg;
    uint16_t len;
    uint32_t range;
    int ret;

    memset(&msg, 0, sizeof(msg));

    ctx->start_tod = -1;
    ctx->last_pts = 0;

    if (ctx->frame_rate > 0) {
        fps.den = ctx->frame_rate;
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

    range = msg.nb_cells;

    while (av_image_check_size(range * 2, range * 2, AV_LOG_TRACE, s) < 0) {
        --range;
    }

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->width = st->codecpar->height = range * 2;
    st->codecpar->codec_id = AV_CODEC_ID_CAT240;
    st->codecpar->format = AV_PIX_FMT_RGB24;
    st->time_base = fps;
    st->start_time = 0;
    st->duration = 0;

    if (range != msg.nb_cells) {
        av_log(s, AV_LOG_WARNING, "Range reduced from %"PRIu32" to %"PRIu32" cells\n", msg.nb_cells, range);
    }

    return 0;

error:
    av_free(msg_buf);
    return AVERROR(EIO);
}

static int asterix_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AsterixContext* ctx = s->priv_data;
    AVIOContext *pb = s->pb;
    uint8_t cat240_hdr = 8, messagetype;
    uint16_t len, fspec, datasource;
    int ret;

    /* read Video Data Block */
    while ((ret = av_append_packet(pb, pkt, cat240_hdr)) > 0) {

        if (ret != cat240_hdr) {
            av_log(s, AV_LOG_ERROR, "Failed to read header: %d\n", ret);
            return AVERROR(EIO);
        }

        if (pkt->data[0] != 0xf0) {
            av_log(s, AV_LOG_ERROR, "Separator 0xf0 not found at %"PRId64"\n", avio_tell(pb));
            return AVERROR(EIO);
        }

        /* We except Standard UAP format with FSPEC size 2 */
        len = AV_RB16(&pkt->data[1]);
        fspec = AV_RB16(&pkt->data[3]);
        datasource = AV_RB16(&pkt->data[5]);
        messagetype = pkt->data[7];

        av_log(s, AV_LOG_DEBUG, "Field Specification: 0x%"PRIx16". Data Source Identifier 0x%"PRIx16". Message type: %"PRIu8", len=%"PRIu16"\n",
               fspec, datasource, messagetype, len);

        if ((ret = av_append_packet(pb, pkt, len - cat240_hdr)) < 0) {
            av_log(s, AV_LOG_ERROR, "Failed to read full packet. Error: %d\n", ret);
            return ret;
        }

        if ((ret = av_append_packet(pb, pkt, 0)) < 0) {
            av_log(s, AV_LOG_ERROR, "Failed to submit packet. Error: %d. %s\n", ret, av_err2str(ret));
            return ret;
        }
        
        /* must be Video Message 002 (Video Summary is 001) */
        if (messagetype == VIDEOSUMMARY_MSGTYPE) {

            double elapsed;

            /* process Time of Day (len = 3) */
            uint32_t tod = AV_RB24(&pkt->data[len - 3]);

            if (ctx->start_tod == -1)
                ctx->start_tod = tod;

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
        } else {
            pkt->stream_index = 0;
            pkt->duration = 0;
            pkt->pts = ctx->last_pts;
            pkt->dts = ctx->last_pts;
        }
        
        return len;
    }

    return ret;
}

static int asterix_read_close(AVFormatContext *s)
{
    return 0;
}

static const AVOption demux_options[] = {
    { .name   = "fps",
      .help   = "Frame rate denominator (1/fps)",
      .offset = offsetof(AsterixContext, frame_rate),
      .type   = AV_OPT_TYPE_INT,
      { .i64  = 30 },
      .min    = 1,
      .max    = INT_MAX,
      .flags  = AV_OPT_FLAG_DECODING_PARAM,
      .unit   = NULL },
    { NULL },
};

static const AVClass asterix_demuxer_class = {
    .class_name = "Asterix demuxer",
    .item_name  = av_default_item_name,
    .option     = demux_options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEMUXER,
};

AVInputFormat ff_asterix_demuxer = {
    .name           = "asterix",
    .long_name      = NULL_IF_CONFIG_SMALL("ASTERIX Radar Video (Eurocontrol Category 240)"),
    .read_probe     = asterix_probe,
    .priv_data_size = sizeof(AsterixContext),
    .read_header    = asterix_read_header,
    .read_packet    = asterix_read_packet,
    .read_close     = asterix_read_close,
    .extensions     = "asterix",
    .flags          = AVFMT_GENERIC_INDEX,
    /* .codec_tag      = (const AVCodecTag* const []){ff_codec_asterix_tags, 0}, */
    .priv_class     = &asterix_demuxer_class,
};
