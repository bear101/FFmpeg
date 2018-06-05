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

#ifndef AVCODEC_CAT240_H
#define AVCODEC_CAT240_H

#include "internal.h"

typedef struct Cat240VideoMessage {
    uint16_t len;
    uint16_t fspec;
    uint16_t datasource;
    uint32_t msgseqid;
    uint16_t start_az, end_az;
    uint32_t start_rg, cell_dur;
    uint16_t vcr_dci;
    uint8_t res;
    uint16_t nb_vb;
    uint32_t nb_cells;
    uint8_t rep;
    uint32_t tod;
    const uint8_t *data;
} Cat240VideoMessage;

/* @return 0 means found CAT 240 Video Message. 1 means other message
 * found. Less than 0 means buf contains invalid data. */
int parse_cat240_videomessage(void *avcl, const uint8_t* buf, uint16_t len,
                              Cat240VideoMessage* msg);

#endif
