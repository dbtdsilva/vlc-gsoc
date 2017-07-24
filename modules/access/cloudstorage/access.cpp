/*****************************************************************************
 * access.cpp: cloud storage access module using libcloudstorage
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

#include "access.h"

#include <algorithm>
#include <sstream>
#include <fstream>
#include <ICloudStorage.h>
#include <IRequest.h>

#include "provider_callback.h"
#include "provider_httpd.h"
#include "provider_http.h"

using cloudstorage::ICloudStorage;
using cloudstorage::ICloudProvider;
using cloudstorage::IDownloadFileCallback;
using cloudstorage::IItem;

static int AddItem( struct access_fsdir *, stream_t *, IItem::Pointer );
static int InitKeystore( stream_t * );
static int InitProvider( stream_t * );
static int ParseUrl( stream_t * );
static int ReadDir( stream_t *, input_item_node_t * );
static std::string ReadFile( const std::string& path );

int Open( vlc_object_t *p_this )
{
    access_t *p_access = (access_t*) p_this;
    access_sys_t *p_sys;

    p_access->p_sys = p_sys = new access_sys_t();
    if ( p_sys == nullptr )
        return VLC_ENOMEM;

    if ( ParseUrl( p_access ) != VLC_SUCCESS )
        goto error;
    if ( InitKeystore( p_access ) != VLC_SUCCESS )
        goto error;
    if ( InitProvider( p_access ) != VLC_SUCCESS )
        goto error;

    if ( p_sys->current_item->type() == IItem::FileType::Directory ) {
        p_access->pf_control = access_vaDirectoryControlHelper;
        p_access->pf_readdir = ReadDir;
        return VLC_SUCCESS;
    } else if ( p_sys->current_item->type() != IItem::FileType::Unknown ) {
        p_sys->current_item = p_sys->provider->
                getItemDataAsync( p_sys->current_item->id() )->result();
        p_access->psz_url = strdup( p_sys->current_item->url().c_str() );
        return VLC_ACCESS_REDIRECT;
    }

error:
    Close( p_this );
    return VLC_EGENERIC;
}

void Close( vlc_object_t *p_this )
{
    access_t *p_access = (access_t*) p_this;
    access_sys_t *p_sys = (access_sys_t*) p_access->p_sys;

    if ( p_sys != nullptr ) {
        if ( p_sys->p_keystore != nullptr && !p_sys->memory_keystore )
            vlc_keystore_release( p_sys->p_keystore );
        delete p_sys;
    }
}

static int InitKeystore( stream_t * p_access )
{
    access_sys_t *p_sys = (access_sys_t *) p_access->p_sys;
    vlc_keystore_entry *p_entries;
    // Keystore is never created when there is no user specified
    p_sys->memory_keystore = p_sys->username.empty();
    if ( p_sys->username.empty() )
    {
        p_sys->p_keystore = vlc_get_memory_keystore( VLC_OBJECT( p_access ) );
        p_sys->username = std::string( "memory_user" );
    }
    else
        p_sys->p_keystore = vlc_keystore_create( p_access );

    if ( p_sys->p_keystore == nullptr ) {
        msg_Err( p_access, "Failed to create keystore" );
        return VLC_EGENERIC;
    }

    VLC_KEYSTORE_VALUES_INIT( p_sys->ppsz_values );
    p_sys->ppsz_values[KEY_PROTOCOL] = strdup( "cloudstorage" );
    p_sys->ppsz_values[KEY_USER] = strdup( p_sys->username.c_str() );
    p_sys->ppsz_values[KEY_SERVER] = strdup( p_sys->provider_name.c_str() );

    if ( vlc_keystore_find( p_sys->p_keystore,
            p_sys->ppsz_values, &p_entries ) > 0 )
    {
        std::string stored_value = std::string((char *) p_entries[0].p_secret,
                                   p_entries[0].i_secret_len);
        if ( !ICloudProvider::deserializeSession(stored_value,
                p_sys->token, p_sys->hints) )
        {
            msg_Warn( p_access, "Cloudstorage found invalid credentials in the "
                    "keystore under %s@%s, it is going to overwrite them.",
                    p_sys->username.c_str(), p_sys->provider_name.c_str() );
        }
    }
    return VLC_SUCCESS;
}

static int InitProvider( stream_t * p_access )
{
    access_sys_t *p_sys = (access_sys_t *) p_access->p_sys;
    ICloudProvider::Hints hints;
    std::string redirect_port = std::to_string(
            var_InheritInteger( p_access, "http-port" ) ) ;

    // Get predifined port and prefix
    hints["redirect_uri_port"] = redirect_port;
    hints["redirect_uri_prefix"] = "/auth";

    // Load custom-made pages
    std::string parent = "cloudstorage";
    parent.append(DIR_SEP);
    if ( p_sys->provider_name == "amazons3" ||
            p_sys->provider_name  == "mega" ||
            p_sys->provider_name  == "owncloud" )
    {
        hints["login_page"] = ReadFile(
                parent + p_sys->provider_name + "_login.html");
        hints["success_page"] = ReadFile(
                parent + p_sys->provider_name + "_success.html");
    }
    else
        hints["success_page"] = ReadFile( parent + "default_success.html" );
    hints["error_page"] = ReadFile( parent + "default_error.html" );

    // Initialize the provider
    p_sys->provider = cloudstorage::ICloudStorage::
            create()->provider( p_sys->provider_name );
    if ( !p_sys->provider ) {
        msg_Err( p_access, "Failed to load the given provider" );
        return VLC_EGENERIC;
    }
    p_sys->provider->initialize({
        p_sys->token,
        std::unique_ptr<Callback>( new Callback( p_access ) ),
        nullptr,
        std::unique_ptr<Http>( new Http( p_access ) ),
        nullptr,
        std::unique_ptr<HttpdFactory>( new HttpdFactory( p_access ) ),
        hints
    });

    msg_Dbg( p_access, "Path: %s", p_sys->path.c_str() );
    p_sys->current_item = p_sys->provider->getItemAsync( p_sys->path )->result();
    if (p_sys->current_item == nullptr) {
        msg_Err( p_access, "Item %s does not exist in the provider %s",
                 p_sys->path.c_str(), p_sys->provider_name.c_str() );
        return VLC_EGENERIC;
    }
    return VLC_SUCCESS;
}

static int AddItem( struct access_fsdir *p_fsdir, stream_t *p_access,
                     IItem::Pointer item )
{
    std::stringstream url;
    int i_type;

    url << p_access->psz_url;
    if ( strlen( p_access->psz_url ) == 0 ||
         p_access->psz_url[strlen( p_access->psz_url ) - 1] != '/' )
        url << "/";
    url << item->filename();
    i_type = item->type() == IItem::FileType::Directory ?
        ITEM_TYPE_DIRECTORY : ITEM_TYPE_FILE;

    return access_fsdir_additem( p_fsdir, url.str().c_str(),
            item->filename().c_str(), i_type, ITEM_NET );
}

static int ReadDir( stream_t *p_access, input_item_node_t *p_node )
{
    access_sys_t *p_sys = (access_sys_t *) p_access->p_sys;
    struct access_fsdir fsdir;

    ICloudProvider::ListDirectoryRequest::Pointer list_directory_request_ =
            p_sys->provider->listDirectoryAsync( p_sys->current_item );

    access_fsdir_init( &fsdir, p_access, p_node );
    fsdir.psz_ignored_exts = strdup("");
    int error_code = VLC_SUCCESS;
    for ( auto &i : list_directory_request_->result() )
    {
        if ( AddItem(&fsdir, p_access, i) != VLC_SUCCESS )
        {
            error_code = VLC_EGENERIC;
            break;
        }
    }
    access_fsdir_finish( &fsdir, error_code );
    return error_code;
}

static int ParseUrl( access_t * p_access )
{
    // Expected MRL is cloudstorage://[user@]{provider}/{path}
    access_sys_t *p_sys = (access_sys_t *) p_access->p_sys;
    std::string url( p_access->psz_url );
    const std::string access_token( "://" );
    const std::size_t pos = url.find(access_token);

    p_sys->protocol = url.substr( 0, pos );
    if ( p_sys->protocol != "cloudstorage" )
        return VLC_EGENERIC;

    std::string full_path = url.substr( pos + access_token.size() );
    size_t pos_provider = full_path.find("/");
    std::string provider_with_user;
    if ( pos_provider == std::string::npos ) {
        provider_with_user = full_path;
        p_sys->path = "/";
    } else {
        provider_with_user = full_path.substr( 0, pos_provider );
        p_sys->path = full_path.substr( pos_provider );
    }

    size_t pos_user = provider_with_user.find_last_of("@");
    if ( pos_user == std::string::npos ) {
        p_sys->provider_name = provider_with_user;
    } else {
        p_sys->username = provider_with_user.substr( 0, pos_user );
        p_sys->provider_name = provider_with_user.substr( pos_user + 1);
    }

    return VLC_SUCCESS;
}

static std::string ReadFile(const std::string& filename)
{
    std::string data_filename = config_GetDataDir();
    data_filename.append(DIR_SEP);
    data_filename.append(filename);
    std::ifstream stream(data_filename, std::ios::in | std::ios::binary);
    if (!stream)
        return "";

    std::string contents;
    stream.seekg(0, std::ios::end);
    contents.resize(stream.tellg());
    stream.seekg(0, std::ios::beg);
    stream.read(&contents[0], contents.size());
    stream.close();

    return contents;
}
