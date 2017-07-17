/*****************************************************************************
 * services_discovery.cpp: cloud storage services discovery module
 *****************************************************************************
 * Copyright (C) 2017
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
#include <sstream>

#include "services_discovery.h"

static int CreateProviderEntry( services_discovery_t * );
static int CreateAssociationEntries( services_discovery_t * );
static int RepresentAuthenticatedUsers( services_discovery_t * );
static char * GenerateUserIdentifier( services_discovery_t *, const char * );
static int NewAuthenticationCallback( vlc_object_t *, char const *,
                     vlc_value_t, vlc_value_t, void * );
static int RequestedFromUI( vlc_object_t *, char const *,
                     vlc_value_t, vlc_value_t, void * );

using cloudstorage::ICloudStorage;

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

    if ( CreateProviderEntry( p_sd ) != VLC_SUCCESS )
        goto error;
    if ( RepresentAuthenticatedUsers ( p_sd ) != VLC_SUCCESS )
        goto error;
    //if ( CreateAssociationEntries ( p_sd ) != VLC_SUCCESS )
    //    goto error;

    // Associate callbacks to detect when a user is authenticated with success!
    if ( var_Create( p_sd->obj.libvlc, "cloud-new-auth",
            VLC_VAR_STRING) != VLC_SUCCESS )
        goto error;
    var_AddCallback( p_sd->obj.libvlc, "cloud-new-auth",
            NewAuthenticationCallback, p_sd );

    if ( var_Create( p_sd->obj.parent, "cloudstorage-request",
            VLC_VAR_STRING ) != VLC_SUCCESS )
        goto error;
    var_AddCallback( p_sd->obj.parent, "cloudstorage-request",
            RequestedFromUI, p_sd );
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
    {
        input_item_Release( p_item_root.second->root );
        input_item_Release( p_item_root.second->service_add );

        delete p_item_root.second;
    }

    var_DelCallback( p_sd->obj.libvlc, "cloud-new-auth",
            NewAuthenticationCallback, p_sd );
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

        provider_item_t * prov = new provider_item_t();
        prov->root = p_item;
        p_sys->providers_items[provider_name] = prov;
        //services_discovery_AddItem( p_sd, p_item );
    }

    return VLC_SUCCESS;
}

static int CreateAssociationEntries( services_discovery_t * p_sd )
{
    services_discovery_sys_t *p_sys = (services_discovery_sys_t *) p_sd->p_sys;

    int error_code;
    for ( const auto& provider : p_sys->providers_items )
    {
        char * uri;
        const char * provider_name = provider.first.c_str();
        char * username = GenerateUserIdentifier( p_sd, provider_name );
        if ( username == nullptr )
            return VLC_EGENERIC;
        error_code = asprintf(&uri, "cloudstorage://%s@%s/", username,
                provider_name );
        free( username );
        if ( error_code < 0)
            return VLC_EGENERIC;

        msg_Dbg( p_sd, "Creating MRL with %s", uri );
        input_item_t *p_item_new = input_item_New( uri,
                "Associate new account" );
        free( uri );

        if ( p_item_new != nullptr )
        {
            services_discovery_AddSubItem( p_sd, provider.second->root,
                    p_item_new );
            provider.second->service_add = p_item_new;
        }
    }
    return VLC_SUCCESS;
}

static int RepresentAuthenticatedUsers( services_discovery_t * p_sd )
{
    services_discovery_sys_t *p_sys = (services_discovery_sys_t *) p_sd->p_sys;

    vlc_keystore_entry *p_entries;
    VLC_KEYSTORE_VALUES_INIT( p_sys->ppsz_values );
    p_sys->ppsz_values[KEY_PROTOCOL] = strdup( "cloudstorage" );
    unsigned int i_entries = vlc_keystore_find( p_sys->p_keystore,
            p_sys->ppsz_values, &p_entries );
    for (unsigned int i = 0; i < i_entries; i++)
    {
        std::map< std::string, provider_item_t * >::iterator prov =
            p_sys->providers_items.find(p_entries[i].ppsz_values[KEY_SERVER]);
        // Invalid provider was found
        if ( prov == p_sys->providers_items.end() )
            continue;

        char *uri, *user_with_provider;
        if ( asprintf(&user_with_provider, "%s@%s",
                p_entries[i].ppsz_values[KEY_USER], prov->first.c_str() ) < 0 )
            return VLC_EGENERIC;

        if ( asprintf(&uri, "cloudstorage://%s/", user_with_provider ) < 0 )
        {
            free( user_with_provider );
            return VLC_EGENERIC;
        }

        input_item_t *p_item_user = input_item_NewDirectory( uri,
                user_with_provider, ITEM_NET );
        if ( p_item_user != NULL ) {
            services_discovery_AddItem( p_sd, p_item_user );
            input_item_Release ( p_item_user );
        }

        free( user_with_provider );
        free( uri );
    }
    return VLC_SUCCESS;
}

static char * GenerateUserIdentifier( services_discovery_t * p_sd,
        const char * provider)
{
    services_discovery_sys_t *p_sys = (services_discovery_sys_t *) p_sd->p_sys;
    // Find the last generated identifier for the Association MRL
    // MRL in the item will contain a predefined user
    vlc_keystore_entry *p_entries;
    VLC_KEYSTORE_VALUES_INIT( p_sys->ppsz_values );
    p_sys->ppsz_values[KEY_PROTOCOL] = strdup( "cloudstorage" );
    p_sys->ppsz_values[KEY_SERVER] = strdup( provider );
    unsigned int i_entries = vlc_keystore_find( p_sys->p_keystore,
            p_sys->ppsz_values, &p_entries );

    unsigned int id = 1;
    bool name_exists;
    char * gen_user;
    do
    {
        if ( asprintf( &gen_user, "user%d", id ) < 0 )
            return nullptr;
        name_exists = false;
        for ( unsigned int i = 0; i < i_entries; i++ )
        {
            if ( strcmp( p_entries[i].ppsz_values[KEY_USER], gen_user ) == 0)
            {
                id += 1;
                name_exists = true;
                free( gen_user );
                break;
            }
        }
    } while ( name_exists );

    return gen_user;
}

static int NewAuthenticationCallback( vlc_object_t *p_this, char const *psz_var,
                     vlc_value_t oldval, vlc_value_t newval, void *p_data )
{
    (void) oldval; (void) p_this; (void) psz_var;
    services_discovery_t * p_sd = (services_discovery_t *) p_data;
    services_discovery_sys_t *p_sys = (services_discovery_sys_t *) p_sd->p_sys;

    std::string provider_name, username;
    std::string provider_with_user( newval.psz_string );
    size_t pos_user = provider_with_user.find_last_of("@");

    if ( pos_user == std::string::npos )
        return VLC_EGENERIC;

    username = provider_with_user.substr( 0, pos_user );
    provider_name = provider_with_user.substr( pos_user + 1);

    const auto& it = p_sys->providers_items.find( provider_name );
    if ( it == p_sys->providers_items.end() )
        return VLC_EGENERIC;

    // Open a new entry with the new authenticated user
    std::stringstream ss;
    ss << "cloudstorage://" << provider_with_user;
    input_item_t *p_item_user = input_item_NewDirectory( ss.str().c_str(),
               username.c_str(), ITEM_NET );
    if ( p_item_user != NULL ) {
        services_discovery_AddSubItem( p_sd, it->second->root,
                p_item_user );
        input_CreateAndStart( p_sd, p_item_user, NULL);
        input_item_Release ( p_item_user );
    }

    // Remove old association
    services_discovery_RemoveItem( p_sd, it->second->service_add );
    input_item_Release( it->second->service_add );

    // Create a new association entry with a new MRL
    std::stringstream uri_add_provider;
    uri_add_provider << "cloudstorage://";
    uri_add_provider << GenerateUserIdentifier( p_sd, provider_name.c_str() );
    uri_add_provider << "@" << provider_name;
    input_item_t *p_item_assoc = input_item_New( uri_add_provider.str().c_str(),
                "Associate new account" );
    if ( p_item_assoc != nullptr ) {
        it->second->service_add = p_item_assoc;
        services_discovery_AddSubItem( p_sd, it->second->root,
                p_item_assoc );
    }

    return VLC_SUCCESS;
}

static int RequestedFromUI( vlc_object_t *p_this, char const *psz_var,
                     vlc_value_t oldval, vlc_value_t newval, void *p_data )
{
    (void) oldval; (void) p_this; (void) psz_var;
    services_discovery_t * p_sd = (services_discovery_t *) p_data;
    services_discovery_sys_t *p_sys = (services_discovery_sys_t *) p_sd->p_sys;

    return VLC_SUCCESS;
}