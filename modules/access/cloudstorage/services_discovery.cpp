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
#include <vlc_keystore.h>

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


    vlc_keystore* p_keystore = vlc_keystore_create( p_sd );
    if ( p_sd == nullptr ) {
        msg_Err( p_sd, "Failed to create keystore" );
        return VLC_EGENERIC;
    }

    char *ppsz_values[KEY_MAX];
    for ( const auto& provider : storage->providers() ) {
        const char * provider_name = provider->name().c_str();

        // Creating keystore already registered value
        vlc_keystore_entry *p_entries;
        VLC_KEYSTORE_VALUES_INIT( ppsz_values );
        ppsz_values[KEY_PROTOCOL] = strdup( "cloudstorage" );
        ppsz_values[KEY_SERVER] = strdup( provider_name );
        unsigned int i_entries = vlc_keystore_find( p_keystore, ppsz_values,
                                                    &p_entries );
        for (unsigned int i = 0; i < i_entries; i++) {
            char *uri, *user_with_provider;
            if ( asprintf(&user_with_provider, "%s@%s",
                    p_entries[i].ppsz_values[KEY_USER], provider_name ) < 0 )
                continue;
            if ( asprintf(&uri, "cloudstorage://%s/", user_with_provider ) < 0 )
            {
                free( user_with_provider );
                continue;
            }
            input_item_t *p_item = input_item_NewDirectory( uri,
                    user_with_provider, ITEM_NET );
            free( user_with_provider );
            free( uri );
            if ( p_item != NULL ) {
                services_discovery_AddItem( p_sd, p_item );
                input_item_Release ( p_item );
            }
        }


        // Creating the clean entries
        char *uri;
        if ( asprintf(&uri, "cloudstorage://%s/", provider_name ) < 0)
            continue;
        input_item_t *p_item = input_item_NewDirectory( uri, provider_name,
                                                        ITEM_NET );
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
