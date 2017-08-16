/*****************************************************************************
 * access.cpp: cloud storage access module using libcloudstorage
 *****************************************************************************
 * Copyright (C) 2017 VideoLabs and VideoLAN
 *
 * Authors: William Ung <williamung@msn.com>
 *          Diogo Silva <dbtdsilva@gmail.com>
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

#ifndef VLC_CLOUDSTORAGE_ACCESS_H
#define VLC_CLOUDSTORAGE_ACCESS_H

#include <string>
#include <ICloudProvider.h>
#include <IItem.h>

#include <vlc_common.h>
#include <vlc_access.h>
#include <vlc_input.h>
#include <vlc_keystore.h>
#include <vlc_url.h>

int Open( vlc_object_t * );
void Close( vlc_object_t * );

struct access_sys_t
{
    // Parsed parameters
    vlc_url_t url;
    bool memory_keystore;

    // Loaded on Open
    cloudstorage::ICloudProvider::Pointer provider;
    std::string token;
    cloudstorage::ICloudProvider::Hints hints;
    cloudstorage::IItem::Pointer current_item;
};

#endif
