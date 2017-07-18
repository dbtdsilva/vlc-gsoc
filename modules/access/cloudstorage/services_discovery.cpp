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
#include <algorithm>

#include "services_discovery.h"

static int GetProvidersList( services_discovery_t * );
static int RepresentUsers( services_discovery_t * );
static input_item_t * GetNewUserInput( services_discovery_t *, const char *,
                     const char * );
static int InsertNewUserInput( services_discovery_t *, input_item_t * );
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

    if ( GetProvidersList( p_sd ) != VLC_SUCCESS )
        goto error;
    if ( RepresentUsers( p_sd ) != VLC_SUCCESS )
        goto error;

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
        input_item_Release( p_item_root.second );

    var_DelCallback( p_sd->obj.libvlc, "cloud-new-auth",
            NewAuthenticationCallback, p_sd );
    var_DelCallback( p_sd->obj.parent, "cloudstorage-request", RequestedFromUI,
            p_sd);
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

    vlc_keystore_entry *p_entries;
    VLC_KEYSTORE_VALUES_INIT( p_sys->ppsz_values );
    p_sys->ppsz_values[KEY_PROTOCOL] = strdup( "cloudstorage" );
    unsigned int i_entries = vlc_keystore_find( p_sys->p_keystore,
                    p_sys->ppsz_values, &p_entries );
    for (unsigned int i = 0; i < i_entries; i++)
    {
        input_item_t * item = GetNewUserInput( p_sd,
                p_entries[i].ppsz_values[KEY_USER],
                p_entries[i].ppsz_values[KEY_SERVER] );
        InsertNewUserInput( p_sd, item );
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
    services_discovery_AddItem( p_sd, item );
    p_sys->providers_items.insert(
        std::make_pair( item->psz_name, item ) );
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

    input_item_t * item = GetNewUserInput( p_sd, username.c_str(),
            provider_name.c_str() );
    return InsertNewUserInput( p_sd, item );
}

static int RequestedFromUI( vlc_object_t *p_this, char const *psz_var,
                     vlc_value_t oldval, vlc_value_t newval, void *p_data )
{
    (void) oldval; (void) p_this; (void) psz_var;
    services_discovery_t * p_sd = (services_discovery_t *) p_data;
    services_discovery_sys_t *p_sys = (services_discovery_sys_t *) p_sd->p_sys;

    std::string request( newval.psz_string );

    std::string operation = request.substr(0, request.find_first_of(":"));
    std::string remaining = request.substr(request.find_first_of(":") + 1);
    fprintf(stderr, "Op: %s, Remaining: %s\n", operation.c_str(), remaining.c_str());
    if ( operation == "ADD" )
    {
        char* gen_user = GenerateUserIdentifier( p_sd, remaining.c_str() );
        input_item_t * new_item = GetNewUserInput( p_sd, gen_user,
                remaining.c_str() );
        input_CreateAndStart( p_sd, new_item, NULL);
    }
    else if ( operation == "RM" )
    {

    }
    
    

    return VLC_SUCCESS;
}