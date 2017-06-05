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

static int AddItem( struct access_fsdir *, stream_t *, IItem::Pointer );
static int InitKeystore( stream_t *);
static int ParseUrl( stream_t * );
static int ReadDir( stream_t *, input_item_node_t * );

int Open( vlc_object_t *p_this )
{
    access_t *p_access = (access_t*) p_this;
    access_sys_t *p_sys;

    ICloudProvider::Hints hints;
    IItem::Pointer item;

    p_access->p_sys = p_sys = (access_sys_t*) calloc( 1, sizeof(access_sys_t) );
    if ( p_sys == nullptr )
        return VLC_ENOMEM;

    if ( ParseUrl( p_access ) != VLC_SUCCESS )
        goto error;
    if ( InitKeystore( p_access) != VLC_SUCCESS )
        goto error;

    if (p_sys->token_ != "")
        hints["access_token"] = p_sys->token_;
    p_sys->provider_ = cloudstorage::ICloudStorage::
            create()->provider( p_sys->provider_name_ );
    if (!p_sys->provider_)
        goto error;
    p_sys->provider_->initialize({
        p_sys->token_,
        std::unique_ptr<Callback>(new Callback( (access_t*)p_this, p_sys) ),
        nullptr,
        nullptr,
        nullptr,
        hints
    });

    msg_Dbg(p_access, "Path: %s", p_sys->path_.c_str());
    item = p_sys->provider_->getItemAsync(p_sys->path_)->result();
    if (item == nullptr) {
        msg_Err(p_access, "Item %s does not exist in the provider %s",
                p_sys->path_.c_str(), p_sys->provider_name_.c_str());
        goto error;
    }

    if (item->type() == IItem::FileType::Directory) {
        p_sys->current_item_ = item;
        p_access->pf_control = access_vaDirectoryControlHelper;
        p_access->pf_readdir = ReadDir;
        return VLC_SUCCESS;
    } else if (item->type() != IItem::FileType::Unknown) {
        item = p_sys->provider_->getItemDataAsync(item->id())->result();
        p_access->psz_url = strdup(item->url().c_str());
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

    free(p_sys);
}

static int InitKeystore( stream_t * p_access ) {
    access_sys_t *p_sys = (access_sys_t *) p_access->p_sys;
    vlc_keystore_entry *p_entries;

    p_sys->p_keystore_ = vlc_keystore_create( p_access );
    if (p_sys->p_keystore_ == nullptr) {
        msg_Err(p_access, "Failed to create keystore");
        return VLC_EGENERIC;
    }

    VLC_KEYSTORE_VALUES_INIT( p_sys->ppsz_values );
    p_sys->ppsz_values[KEY_PROTOCOL] = strdup("cloudstorage");
    p_sys->ppsz_values[KEY_USER] = strdup("cloudstorage user");
    p_sys->ppsz_values[KEY_SERVER] = strdup(p_sys->provider_name_.c_str());

    if ( vlc_keystore_find( p_sys->p_keystore_,
            p_sys->ppsz_values, &p_entries ) > 0 ) {
        p_sys->token_ = std::string((char *) p_entries[0].p_secret,
                p_entries[0].i_secret_len);
    }
    return VLC_SUCCESS;
}

static int AddItem( struct access_fsdir *p_fsdir, stream_t *p_access,
                     IItem::Pointer item )
{
    std::stringstream url;
    int i_type;

    url << p_access->psz_url;
    if (strlen( p_access->psz_url ) == 0 ||
            p_access->psz_url[strlen( p_access->psz_url ) - 1] != '/')
        url << "/";
    url << item->filename();
    i_type = item->type() == IItem::FileType::Directory ?
        ITEM_TYPE_DIRECTORY : ITEM_TYPE_FILE;

    return access_fsdir_additem( p_fsdir, url.str().c_str(), item->filename().c_str(),
                                 i_type, ITEM_NET );
}

static int ReadDir( stream_t *p_access, input_item_node_t *p_node )
{
    access_sys_t *p_sys = (access_sys_t *) p_access->p_sys;
    struct access_fsdir fsdir;

    p_sys->list_directory_request_ = p_sys->provider_->
            listDirectoryAsync( p_sys->current_item_ );
    p_sys->directory_list_ = p_sys->list_directory_request_->result();

    access_fsdir_init( &fsdir, p_access, p_node );
    int error_code = VLC_SUCCESS;
    msg_Dbg(p_access, "PSZ_URL: %s", p_access->psz_url);
    for ( auto &i : p_sys->directory_list_ )
    {
        msg_Dbg(p_access, "File found: %s", i->filename().c_str());
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
    access_sys_t *p_sys = (access_sys_t *) p_access->p_sys;
    std::string url( p_access->psz_url );
    const std::string access_token( "://" );
    const std::size_t pos = url.find(access_token);

    p_sys->protocol_ = url.substr( 0, pos );
    if (p_sys->protocol_ != "cloudstorage")
        return VLC_EGENERIC;

    std::string parsed;
    std::istringstream iss( url.substr( pos + access_token.size() ) );
    if ( !std::getline(iss, parsed, '/') )
        return VLC_EGENERIC;
    p_sys->provider_name_ = parsed;

    while( std::getline(iss, parsed, '/') ) {
        p_sys->path_.append("/");
        p_sys->path_.append(parsed);
        p_sys->directory_stack_.push_back(parsed);
    }
    if (p_sys->path_.empty()) p_sys->path_ = "/";
    return VLC_SUCCESS;
}