/*****************************************************************************
 * services_discovery.cpp: cloud storage services discovery module
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <ICloudStorage.h>

#include "services_discovery.h"

using cloudstorage::ICloudStorage;

int vlc_sd_probe_Open( vlc_object_t *obj )
{
    vlc_probe_t *probe = (vlc_probe_t *) obj;
    return vlc_sd_probe_Add( probe, N_("cloudstorage"), N_("Cloud Storage"),
        SD_CAT_INTERNET );
}

int SDOpen( vlc_object_t *p_this ) {
    services_discovery_t *p_sd = (services_discovery_t *) p_this;
    p_sd->description = _( "Cloud Storage" );

    msg_Dbg( p_sd, "Opened Services Discovery" );
    cloudstorage::ICloudStorage::Pointer storage = cloudstorage::
            ICloudStorage::create();
    for ( const auto& provider : storage->providers() ) {
        const char * provider_name = provider->name().c_str();

        char *uri;
        if ( asprintf(&uri, "cloudstorage://" ) < 0)
            continue;
        input_item_t *p_item = input_item_New( uri, provider_name );
        free( uri );

        if ( p_item != NULL ) {
            services_discovery_AddItem( p_sd, p_item );
            input_item_Release ( p_item );
        }
    }

    msg_Dbg( p_sd, "SDOpen ended" );
    return VLC_SUCCESS;
}

void SDClose( vlc_object_t *p_this ) {
    msg_Dbg( p_this, "Closed Services Discovery" );
}
