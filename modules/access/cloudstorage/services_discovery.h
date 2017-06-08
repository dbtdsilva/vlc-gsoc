/*****************************************************************************
 * services_discovery.h: cloud storage services discovery module
 *****************************************************************************
 * Copyright (C) 2017 William Ung
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

#include <vector>
#include <map>
#include <string>

#include <vlc_common.h>
#include <vlc_keystore.h>
#include <vlc_services_discovery.h>

int vlc_sd_probe_Open( vlc_object_t * );
int SDOpen( vlc_object_t * );
void SDClose( vlc_object_t * );

struct provider_item_t
{
    input_item_t * root;
    input_item_t * service_add;
};

struct services_discovery_sys_t
{
    vlc_thread_t thread;
    vlc_keystore *p_keystore;
    std::map< std::string, provider_item_t * > providers_items;
    char *ppsz_values[KEY_MAX];
};