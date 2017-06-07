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

static int CreateProviderEntry( services_discovery_t * );

using cloudstorage::ICloudStorage;

int vlc_sd_probe_Open( vlc_object_t *obj )
{
    vlc_probe_t *probe = (vlc_probe_t *) obj;
    return vlc_sd_probe_Add( probe, N_("cloudstorage"), N_("Cloud Storage"),
        SD_CAT_INTERNET );
}

int SDOpen( vlc_object_t *p_this )
{
    services_discovery_t *p_sd = (services_discovery_t *) p_this;
    services_discovery_sys_t *p_sys;

    p_sd->p_sys = p_sys = new services_discovery_sys_t();
    if ( p_sys == nullptr)
        return VLC_ENOMEM;
    p_sd->description = _( "Cloud Storage" );

    p_sys->p_keystore = vlc_keystore_create( p_sd );
    if ( p_sys->p_keystore == nullptr )
    {
        msg_Err( p_sd, "Failed to create keystore" );
        goto error;
    }

    if ( CreateProviderEntry( p_sd) != VLC_SUCCESS )
        goto error;

    //for ( const auto& provider : storage->providers() ) {
    for ( const auto& provider_root : p_sys->providers_items )
    {
        const char* provider_name = provider_root.first.c_str();
        input_item_t *p_root = provider_root.second;

        char *uri;
        if ( asprintf(&uri, "cloudstorage://newuser@%s/", provider_name ) < 0)
            continue;
        input_item_t *p_item_new = input_item_NewCard( uri,
                "Associate new account" );
        free( uri );

        services_discovery_AddSubItem( p_sd, p_root, p_item_new );

        // Creating keystore already registered value
        vlc_keystore_entry *p_entries;
        VLC_KEYSTORE_VALUES_INIT( p_sys->ppsz_values );
        p_sys->ppsz_values[KEY_PROTOCOL] = strdup( "cloudstorage" );
        p_sys->ppsz_values[KEY_SERVER] = strdup( provider_name );
        unsigned int i_entries = vlc_keystore_find( p_sys->p_keystore,
                p_sys->ppsz_values, &p_entries );
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
            input_item_t *p_item_user = input_item_NewDirectory( uri,
                    user_with_provider, ITEM_NET );
            if ( p_item_user != NULL ) {
                services_discovery_AddSubItem( p_sd, p_root, p_item_user );
                input_item_Release ( p_item_user );
            }

            free( user_with_provider );
            free( uri );
        }
    }

    msg_Dbg( p_sd, "SDOpen ended" );
    return VLC_SUCCESS;

error:
    SDClose( p_this );
    return VLC_EGENERIC;
}

void SDClose( vlc_object_t *p_this )
{
    services_discovery_t *p_sd = (services_discovery_t *) p_this;
    services_discovery_sys_t *p_sys = (services_discovery_sys_t*) p_sd->p_sys;

    if ( p_sys->p_keystore != nullptr )
        vlc_keystore_release( p_sys->p_keystore );

    for ( auto& p_item_root : p_sys->providers_items )
        input_item_Release( p_item_root.second );

    delete p_sys;
}

static int CreateProviderEntry( services_discovery_t * p_sd )
{
    services_discovery_sys_t *p_sys = (services_discovery_sys_t *) p_sd->p_sys;

    cloudstorage::ICloudStorage::Pointer storage = cloudstorage::
            ICloudStorage::create();
    for ( const auto& provider : storage->providers() )
    {
        const std::string& provider_name = provider->name();
        input_item_t *p_item = input_item_NewExt("vlc://nop",
                _(provider_name.c_str()), -1, ITEM_TYPE_NODE, ITEM_LOCAL);
        if ( p_item == nullptr )
            continue;

        p_sys->providers_items[provider_name] = p_item;
        services_discovery_AddItem( p_sd, p_item );
    }

    return VLC_SUCCESS;
}
