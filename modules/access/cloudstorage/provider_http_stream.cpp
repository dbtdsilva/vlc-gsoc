/*****************************************************************************
 * provider_http_stream.cpp: Stream for sending a payload to the request
 *****************************************************************************
 * Copyright (C) 2017
 *
 * Authors: Diogo Silva <dbtdsilva@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include "provider_http_stream.h"

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <vlc_common.h>
#include <vlc_block.h>

#include <istream>

#include "access/http/message.h"

struct vlc_payload_stream
{
    struct vlc_http_stream stream;
    std::istream *payload_stream;
};

static struct vlc_http_msg *vlc_payload_wait(struct vlc_http_stream *stream)
{
    /* There cannot be headers during this stream. */
    (void) stream;
    return NULL;
}

static block_t *vlc_payload_read(struct vlc_http_stream *stream)
{
    struct vlc_payload_stream *s =
        container_of(stream, struct vlc_payload_stream, stream);
    block_t *block;

    if (s->payload_stream->eof())
        return NULL;

    size_t size = 16384;
    block = block_Alloc(size);
    if (unlikely(block == NULL))
        return NULL;

    s->payload_stream->read((char *) block->p_buffer, size);
    block->i_buffer = s->payload_stream->gcount();

    return block;
}

static void vlc_payload_close(struct vlc_http_stream *stream, bool abort)
{
    struct vlc_payload_stream *s =
        container_of(stream, struct vlc_payload_stream, stream);
    (void) abort;
}

static struct vlc_http_stream_cbs vlc_payload_callbacks =
{
    vlc_payload_wait,
    vlc_payload_read,
    vlc_payload_close,
};

struct vlc_http_stream *vlc_payload_stream_open(std::istream *payload_stream)
{
    struct vlc_payload_stream *s = (vlc_payload_stream*) malloc(sizeof (*s));
    if (unlikely(s == NULL))
        return NULL;

    s->stream.cbs = &vlc_payload_callbacks;
    s->payload_stream = payload_stream;
    return &s->stream;
}