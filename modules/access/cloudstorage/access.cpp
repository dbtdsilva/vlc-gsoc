/*****************************************************************************
 * access.cpp: cloud storage access module using libcloudstorage
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

#include "access.h"

#include <algorithm>
#include <sstream>
#include <ICloudStorage.h>
#include <IRequest.h>

#include "provider_callback.h"

using cloudstorage::ICloudStorage;
using cloudstorage::ICloudProvider;
using cloudstorage::IDownloadFileCallback;
using cloudstorage::IItem;

static int add_item( struct access_fsdir *, stream_t *, IItem::Pointer );
static int readDir( stream_t *, input_item_node_t * );
static std::vector<std::string> parseUrl( std::string );
static int getDir( stream_t *, input_item_node_t * );

int Open( vlc_object_t *p_this )
{
    access_t *p_access = (access_t*) p_this;
    access_sys_t *p_sys;

    ICloudProvider::Hints hints;

    p_access->p_sys = p_sys = (access_sys_t*) calloc( 1, sizeof(access_sys_t) );
    if ( p_sys == nullptr )
        return VLC_ENOMEM;

    vlc_keystore_entry *p_entries;

    std::vector<std::string> dir_stack = parseUrl( p_access->psz_url );
    p_sys->provider_name_ = dir_stack[1];

    p_sys->p_keystore_ = vlc_keystore_create( p_access );
    if (p_sys->p_keystore_ == nullptr) {
        msg_Err(p_access, "Failed to create keystore");
        goto error;
    }
    p_sys->provider_ = cloudstorage::ICloudStorage::
            create()->provider( p_sys->provider_name_ );
    if (!p_sys->provider_) {
        msg_Err(p_access, "Failed to create provider");
        goto error;
    }

    VLC_KEYSTORE_VALUES_INIT( p_sys->ppsz_values );
    p_sys->ppsz_values[KEY_PROTOCOL] = "cloudstorage";
    p_sys->ppsz_values[KEY_USER] = "cloudstorage user";
    p_sys->ppsz_values[KEY_SERVER] = "cloudstorage";

    if ( vlc_keystore_find( p_sys->p_keystore_, p_sys->ppsz_values, &p_entries ) > 0 ) {
        p_sys->token_ = std::string((char *) p_entries[0].p_secret, p_entries[0].i_secret_len);
    }


    if (p_sys->token_ != "")
        hints["access_token"] = p_sys->token_;
    p_sys->provider_->initialize({
        p_sys->token_,
        std::unique_ptr<Callback>(new Callback( (access_t*)p_this, p_sys) ),
        nullptr,
        nullptr,
        nullptr,
        hints
    });

    p_sys->current_item_ = p_sys->provider_->rootDirectory();
    p_access->pf_control = access_vaDirectoryControlHelper;
    p_access->pf_readdir = getDir;
    msg_Dbg(p_this, "URL: %s", p_access->psz_url);

    return VLC_SUCCESS;

 error:
    Close( p_this );
    return VLC_EGENERIC;
}

void Close( vlc_object_t *p_this )
{
    access_t *p_access = (access_t*) p_this;
    access_sys_t *p_sys = (access_sys_t*) p_access->p_sys;

    free(p_sys);
}

static int add_item( struct access_fsdir *p_fsdir, stream_t *p_access,
                     IItem::Pointer item )
{
    std::string url;
    int i_type;

    url = p_access->psz_url + item->filename();
    i_type = item->type() == IItem::FileType::Directory ?
        ITEM_TYPE_DIRECTORY : ITEM_TYPE_FILE;

    return access_fsdir_additem( p_fsdir, url.c_str(), item->filename().c_str(),
                                 i_type, ITEM_NET );
}

static int readDir( stream_t *p_access, input_item_node_t *p_node )
{
    access_sys_t *p_sys = (access_sys_t *) p_access->p_sys;
    struct access_fsdir fsdir;

    p_sys->list_directory_request_ = p_sys->provider_->
            listDirectoryAsync( p_sys->current_item_ );
    p_sys->directory_list_ = p_sys->list_directory_request_->result();

    access_fsdir_init( &fsdir, p_access, p_node );
    int error_code = VLC_SUCCESS;
    for ( auto &i : p_sys->directory_list_ )
    {
        if ( add_item(&fsdir, p_access, i) != VLC_SUCCESS )
        {
            error_code = VLC_EGENERIC;
            break;
        }
    }
    access_fsdir_finish( &fsdir, error_code );
    return error_code;
}

static std::vector<std::string> parseUrl( std::string url )
{
    const std::string access_token( "://" );
    const std::size_t pos = url.find(access_token);

    std::vector<std::string> parts;
    parts.push_back( url.substr( 0, pos ) );

    std::istringstream iss( url.substr( pos + access_token.size() ) );
    for ( std::string s; std::getline(iss, s, '/'); )
        parts.push_back(s);

    return parts;
}

static int getDir( stream_t *p_access, input_item_node_t *p_node )
{
    msg_Dbg(p_access, "get dir: %s; %s; %s", p_access->psz_url,
            p_access->psz_filepath, p_access->psz_location);
    access_sys_t *p_sys = (access_sys_t *) p_access->p_sys;
    std::vector<std::string> url_parts = parseUrl(p_access->psz_url);

    // It is a must to exist the access protocol and the service provider
    // at least. E.g: cloudstorage://dropbox
    if (url_parts.size() < 2)
        return VLC_EGENERIC;

    const std::string& access_protocol = url_parts[0];
    const std::string& provider_name = url_parts[1];

    // Checking if there is a path on the URL or not
    if ( url_parts.size() > 2 )
    {
        p_sys->directory_stack_ = std::vector<std::string>(
                        url_parts.begin() + 2, url_parts.end() );
        for (auto &name : p_sys->directory_stack_)
        {
            p_sys->list_directory_request_ = p_sys->provider_->
                    listDirectoryAsync( p_sys->current_item_ );
            auto v = p_sys->list_directory_request_->result();
            p_sys->current_item_ = *std::find_if( v.begin(),v.end(),
                                        [&name](IItem::Pointer item)
                                        { return item->filename() == name; });
        }
    }

    return readDir( p_access, p_node );
}
