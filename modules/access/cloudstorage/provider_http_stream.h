/*****************************************************************************
 * provider_http_stream.h: Stream for sending a payload to the request
 *****************************************************************************
 * Copyright (C) 2017 VideoLabs and VideoLAN
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

#ifndef VLC_CLOUDSTORAGE_PROVIDER_HTTP_STREAM_H
#define VLC_CLOUDSTORAGE_PROVIDER_HTTP_STREAM_H

#include <istream>

struct vlc_http_stream *vlc_payload_stream_open( std::istream *payload_stream );

#endif