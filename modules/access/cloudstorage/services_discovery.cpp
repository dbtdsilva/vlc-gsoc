/*****************************************************************************
 * services_discovery.cpp: cloud storage services discovery module
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <vlc_keystore.h>
#include <vlc_url.h>

#include <ICloudStorage.h>
#include <sstream>
#include <algorithm>

#include "services_discovery.h"

static int GetProvidersList( services_discovery_t * );
static int RepresentUsers( services_discovery_t * );
static input_item_t * GetNewUserInput( services_discovery_t *, const char *,
                     const char * );
static int InsertNewUserInput( services_discovery_t *, input_item_t * );
static char * GenerateUserIdentifier( services_discovery_t *, const char * );
static int CallbackNewAuthentication( vlc_object_t *, char const *,
                     vlc_value_t, vlc_value_t, void * );
static int CallbackRequestedFromUI( vlc_object_t *, char const *,
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

    if ( GetProvidersList( p_sd ) != VLC_SUCCESS )
        goto error;
    if ( RepresentUsers( p_sd ) != VLC_SUCCESS )
        goto error;

    // Associate callbacks to detect when a user is authenticated with success!
    if ( var_Create( p_sd->obj.libvlc, "cloudstorage-new-auth",
            VLC_VAR_STRING) != VLC_SUCCESS )
        goto error;
    var_AddCallback( p_sd->obj.libvlc, "cloudstorage-new-auth",
            CallbackNewAuthentication, p_sd );

    if ( var_Create( p_sd->obj.parent, "cloudstorage-request",
            VLC_VAR_STRING ) != VLC_SUCCESS )
        goto error;
    var_AddCallback( p_sd->obj.parent, "cloudstorage-request",
            CallbackRequestedFromUI, p_sd );
    return VLC_SUCCESS;

error:
    SDClose( p_this );
    return VLC_EGENERIC;
}

void SDClose( vlc_object_t *p_this )
{
    services_discovery_t *p_sd = (services_discovery_t *) p_this;
    services_discovery_sys_t *p_sys = (services_discovery_sys_t*) p_sd->p_sys;

    for ( auto& p_item_root : p_sys->providers_items )
    {
        input_item_Release( p_item_root.second->item );
        input_Stop( p_item_root.second->thread );
        input_Close( p_item_root.second->thread );
        delete p_item_root.second;
    }

    if (p_sys->auth_thread != nullptr)
    {
        input_Stop( p_sys->auth_thread );
        input_Close( p_sys->auth_thread );
    }

    var_DelCallback( p_sd->obj.libvlc, "cloudstorage-new-auth",
            CallbackNewAuthentication, p_sd );
    var_DelCallback( p_sd->obj.parent, "cloudstorage-request",
            CallbackRequestedFromUI, p_sd);
    delete p_sys;
}

static int GetProvidersList( services_discovery_t * p_sd )
{
    services_discovery_sys_t *p_sys = (services_discovery_sys_t *) p_sd->p_sys;
    for ( const auto& provider_ptr : ICloudStorage::create()->providers() )
        p_sys->providers_list.push_back( provider_ptr->name() );

    if ( p_sys->providers_list.empty() )
    {
        msg_Err( p_sd, "Failed to load providers from libcloudstorage" );
        return VLC_EGENERIC;
    }
    return VLC_SUCCESS;
}

static int RepresentUsers( services_discovery_t * p_sd )
{
    services_discovery_sys_t *p_sys = (services_discovery_sys_t *) p_sd->p_sys;

    for ( auto& provider : p_sys->providers_list )
    {
        std::string mrl_base = "cloudstorage://" + provider;
        vlc_url_t dummy_url;
        vlc_UrlParse( &dummy_url, mrl_base.c_str() );
        vlc_credential cred;
        vlc_credential_init( &cred, &dummy_url );
        vlc_credential_get( &cred, p_sd, NULL, NULL, NULL, NULL );
        for (unsigned int i = 0; i < cred.i_entries_count; i++)
        {
            input_item_t * item = GetNewUserInput( p_sd,
                    cred.p_entries[i].ppsz_values[KEY_USER],
                    cred.p_entries[i].ppsz_values[KEY_SERVER] );
            InsertNewUserInput( p_sd, item );
        }
        vlc_credential_clean( &cred );
        vlc_UrlClean( &dummy_url );
    }
    return VLC_SUCCESS;
}

static input_item_t * GetNewUserInput( services_discovery_t * p_sd,
        const char * user, const char * provider )
{
    services_discovery_sys_t *p_sys = (services_discovery_sys_t *) p_sd->p_sys;

    if ( std::find( p_sys->providers_list.begin(),
                    p_sys->providers_list.end(),
                    provider ) ==
            p_sys->providers_list.end() )
        return nullptr;

    char *user_with_provider;
    if ( asprintf( &user_with_provider, "%s@%s", user, provider ) < 0 )
        return nullptr;
    char *uri;
    if ( asprintf(&uri, "cloudstorage://%s/", user_with_provider ) < 0 )
    {
        free( user_with_provider );
        return nullptr;
    }

    input_item_t *p_item_user = input_item_NewDirectory( uri,
                user_with_provider, ITEM_NET );
    free( user_with_provider );
    free( uri );

    return p_item_user;
}

static int InsertNewUserInput( services_discovery_t * p_sd, input_item_t* item )
{
    if ( item == NULL )
        return VLC_EGENERIC;

    services_discovery_sys_t *p_sys = (services_discovery_sys_t *) p_sd->p_sys;

    input_thread_t * thread = input_CreateAndStart( p_sd, item, NULL);
    services_discovery_AddItem( p_sd, item );

    provider_item * prov = new provider_item();
    prov->item = item;
    prov->thread = thread;
    p_sys->providers_items.insert( std::make_pair( item->psz_name, prov ) );
    return VLC_SUCCESS;
}

static char * GenerateUserIdentifier( services_discovery_t * p_sd,
        const char * provider)
{
    // Find the last generated identifier for the Association MRL
    // MRL in the item will contain a predefined user
    std::string mrl_base = "cloudstorage://";
    mrl_base += provider;
    vlc_url_t dummy_url;
    vlc_UrlParse( &dummy_url, mrl_base.c_str() );
    vlc_credential cred;
    vlc_credential_init( &cred, &dummy_url );
    vlc_credential_get( &cred, p_sd, NULL, NULL, NULL, NULL );

    unsigned int id = 1;
    bool name_exists;
    char * gen_user;
    do
    {
        if ( asprintf( &gen_user, "user%d", id ) < 0 )
            return nullptr;
        name_exists = false;
        for ( unsigned int i = 0; i < cred.i_entries_count; i++ )
        {
            if ( strcmp( cred.p_entries[i].ppsz_values[KEY_USER],
                         gen_user ) == 0)
            {
                id += 1;
                name_exists = true;
                free( gen_user );
                break;
            }
        }
    } while ( name_exists );

    vlc_credential_clean( &cred );
    vlc_UrlClean( &dummy_url );

    return gen_user;
}

static int CallbackNewAuthentication( vlc_object_t *p_this, char const *psz_var,
                     vlc_value_t oldval, vlc_value_t newval, void *p_data )
{
    (void) oldval; (void) p_this; (void) psz_var;
    services_discovery_t * p_sd = (services_discovery_t *) p_data;

    // Closing the thread used to authenticated if exists
    if ( p_sd->p_sys->auth_thread != nullptr )
    {
        input_Stop( p_sd->p_sys->auth_thread );
        input_Close( p_sd->p_sys->auth_thread );
        p_sd->p_sys->auth_thread = nullptr;
    }

    // Process the request
    std::string provider_name, username;
    std::string provider_with_user( newval.psz_string );
    size_t pos_user = provider_with_user.find_last_of("@");

    if ( pos_user == std::string::npos )
        return VLC_EGENERIC;

    username = provider_with_user.substr( 0, pos_user );
    provider_name = provider_with_user.substr( pos_user + 1);

    input_item_t * item = GetNewUserInput( p_sd, username.c_str(),
            provider_name.c_str() );
    return InsertNewUserInput( p_sd, item );
}

static int CallbackRequestedFromUI( vlc_object_t *p_this, char const *psz_var,
                     vlc_value_t oldval, vlc_value_t newval, void *p_data )
{
    (void) oldval; (void) p_this; (void) psz_var;
    services_discovery_t * p_sd = (services_discovery_t *) p_data;
    services_discovery_sys_t *p_sys = (services_discovery_sys_t *) p_sd->p_sys;

    std::string raw( newval.psz_string );

    std::string operation = raw.substr( 0, raw.find_first_of(":") );
    std::string request = raw.substr( raw.find_first_of(":") + 1 );
    if ( operation == "ADD" )
    {
        // Only one add operation is allowed at the same time
        if (p_sys->auth_thread != nullptr)
        {
            msg_Err( p_sd, "There is already a running authentication in "
                     "progress that must be finished before!");
            return VLC_EGENERIC;
        }

        // Generate the user and spawn the authorization
        char* gen_user = GenerateUserIdentifier( p_sd, request.c_str() );
        input_item_t * new_item = GetNewUserInput( p_sd, gen_user,
                request.c_str() );
        p_sys->auth_thread = input_CreateAndStart( p_sd, new_item, NULL);
        input_item_Release( new_item );
    }
    else if ( operation == "RM" )
    {
        std::string mrl_base = "cloudstorage://" + request;
        vlc_url_t dummy_url;
        vlc_UrlParse( &dummy_url, mrl_base.c_str() );
        vlc_credential cred;
        vlc_credential_init( &cred, &dummy_url );
        vlc_credential_get( &cred, p_sd, NULL, NULL, NULL, NULL );
        if ( vlc_credential_delete( &cred, p_sd ) )
        {
            auto it = p_sys->providers_items.find( request );
            services_discovery_RemoveItem( p_sd, it->second->item );
            input_item_Release( it->second->item );

            input_Stop( it->second->thread );
            input_Close( it->second->thread );
            p_sys->providers_items.erase( it );
        }
        vlc_credential_clean( &cred );
    }

    return VLC_SUCCESS;
}