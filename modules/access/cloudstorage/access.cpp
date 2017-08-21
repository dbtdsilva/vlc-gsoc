/*****************************************************************************
 * access.cpp: cloud storage access module using libcloudstorage
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

#include "access.h"

#include <sstream>
#include <fstream>
#include <ICloudStorage.h>

#include <vlc_access.h>
#include <vlc_input.h>
#include <vlc_keystore.h>
#include <vlc_interrupt.h>

#include "provider_callback.h"
#include "provider_httpd.h"
#include "provider_http.h"

using cloudstorage::ICloudStorage;
using cloudstorage::ICloudProvider;
using cloudstorage::IDownloadFileCallback;
using cloudstorage::IItem;
using cloudstorage::IRequest;

static int AddItem( struct access_fsdir *, stream_t *, IItem::Pointer );
static int GetCredentials( stream_t * );
static int InitProvider( stream_t * );
static int ReadDir( stream_t *, input_item_node_t * );
static std::string ReadFile( const std::string& path );
static int ParseMRL( stream_t * );

template <class Type, class T, class ...Args>
static auto WrapLibcloudstorageFunct( stream_t * p_access, Type function,
                                      T&& obj, Args... args )
{
    // Semaphore is used to prevent libcloudstorage to use its own
    // mechanisms to lock, this way it is possible to control the flux.
    vlc_sem_t sem;
    vlc_sem_init( &sem, 0 );
    // Callback to increment semaphore (unlock) when the operation finish
    auto finish_operation = [ &sem ]( auto ) { vlc_sem_post( &sem ); };
    // Invoke the operation
    auto request = (obj.*function)(args..., finish_operation);
    // Wait for the semaphore to finish using vlc mechanisms to lock
    int code = vlc_sem_wait_i11e( &sem );
    vlc_sem_destroy( &sem );
    // Verify if the operation was interrupted or not (cancelling will cause
    // an error to be on the left()
    if ( code == EINTR )
        request->cancel();

    // Retrieve the result and return it
    auto error = request->result().left();
    if ( error )
    {
        msg_Err( p_access, "Failed to process the request (%d): %s",
                 error->code_, error->description_.c_str() );
    }
    return request->result().right();
}

int Open( vlc_object_t *p_this )
{
    int err;
    stream_t *p_access = (stream_t*) p_this;
    access_sys_t *p_sys;

    p_access->p_sys = p_sys = new access_sys_t();
    if ( p_sys == nullptr )
        return VLC_ENOMEM;

    if ( (err = ParseMRL( p_access )) != VLC_SUCCESS )
        goto error;
    if ( (err = GetCredentials( p_access )) != VLC_SUCCESS )
        goto error;
    if ( (err = InitProvider( p_access )) != VLC_SUCCESS )
        goto error;

    if ( p_sys->current_item->type() == IItem::FileType::Directory )
    {
        // If it is a directory, it will be required to ask the libcloudstorage
        // to list all its items
        p_access->pf_control = access_vaDirectoryControlHelper;
        p_access->pf_readdir = ReadDir;
        return VLC_SUCCESS;
    }
    else
    {
        // Obtain the URL from the current item and redirect it to the HTTP
        auto request = WrapLibcloudstorageFunct( p_access,
                &ICloudProvider::getItemDataAsync, *p_sys->provider,
                p_sys->current_item->id() );
        if ( request == nullptr )
        {
            err = VLC_EGENERIC;
            goto error;
        }
        // Redirect to another module with the new URL
        p_access->psz_url = strdup( request->url().c_str() );
        err = VLC_ACCESS_REDIRECT;
    }

error:
    Close( p_this );
    return err;
}

void Close( vlc_object_t *p_this )
{
    stream_t *p_access = (stream_t *) p_this;
    access_sys_t *p_sys = (access_sys_t *) p_access->p_sys;

    if ( p_sys != nullptr ) {
        vlc_UrlClean( &p_sys->url );
        if ( p_sys->alloc_path != nullptr )
            free( p_sys->alloc_path );
        if ( p_sys->alloc_username != nullptr )
            free( p_sys->alloc_username );
        delete p_sys;
    }
}

static int ParseMRL( stream_t * p_access )
{
    access_sys_t *p_sys = (access_sys_t *) p_access->p_sys;

    // Correct the path if there are any invalid characters like spaces and
    // Parse the URL into a VLC object
    char *url = vlc_uri_fixup( p_access->psz_url );
    int error = vlc_UrlParse( &p_sys->url, url );
    free( url );
    if ( error != VLC_SUCCESS )
        return error;

    // If no path was specific, then it is root
    if ( p_sys->url.psz_path == NULL )
    {
        p_sys->alloc_path = strdup("/");
        p_sys->url.psz_path = p_sys->alloc_path;
    }
    // Since there is no info to store the authenticated user, creates a dummy
    // user and stores it temporarly under a in-memory credentials.
    if ( p_sys->url.psz_username == NULL )
    {
        p_sys->memory_keystore = true;
        p_sys->alloc_username = strdup("_dummy_user");
        p_sys->url.psz_username = p_sys->alloc_username;
    }

    return VLC_SUCCESS;
}

static int GetCredentials( stream_t * p_access )
{
    access_sys_t *p_sys = (access_sys_t *) p_access->p_sys;
    // Init credentials and clean within the same scope to prevent the keystore
    // to be loaded at all times
    vlc_credential credentials;
    vlc_credential_init( &credentials, &p_sys->url );
    if ( vlc_credential_get( &credentials, p_access,
            NULL, NULL, NULL, NULL) )
    {
        std::string stored_value = std::string( credentials.psz_password );
        if ( !ICloudProvider::deserializeSession(stored_value,
                p_sys->token, p_sys->hints) )
        {
            msg_Warn( p_access, "Cloudstorage found invalid credentials in the "
                    "keystore under %s@%s, it is going to overwrite them.",
                    p_sys->url.psz_username, p_sys->url.psz_host );
        }
    }
    vlc_credential_clean( &credentials );
    return VLC_SUCCESS;
}

static int InitProvider( stream_t * p_access )
{
    access_sys_t *p_sys = (access_sys_t *) p_access->p_sys;
    ICloudProvider::Hints hints;
    std::string redirect_port = std::to_string(
            config_GetInt( p_access, "http-port" ) );

    // Get predifined port and prefix
    hints["redirect_uri"] = "http://localhost:" + redirect_port + "/auth";
    hints["file_url"] = "http://localhost:" + redirect_port + "/files";

    // Load custom-made pages
    std::string parent = "cloudstorage";
    parent.append( DIR_SEP );
    if ( strcmp( p_sys->url.psz_host, "amazons3" ) == 0 ||
         strcmp( p_sys->url.psz_host, "mega" ) == 0 ||
         strcmp( p_sys->url.psz_host, "owncloud" ) == 0 )
    {
        hints["login_page"] = ReadFile(
                parent + p_sys->url.psz_host + "_login.html");
        hints["success_page"] = ReadFile(
                parent + p_sys->url.psz_host + "_success.html");
    }
    else
        hints["success_page"] = ReadFile( parent + "default_success.html" );
    hints["error_page"] = ReadFile( parent + "default_error.html" );

    // Initialize the provider
    p_sys->provider = cloudstorage::ICloudStorage::create()->provider(
            p_sys->url.psz_host, {
        p_sys->token,
        std::unique_ptr<Callback>( new Callback( p_access ) ),
        nullptr,
        std::unique_ptr<Http>( new Http(p_access ) ),
        std::unique_ptr<HttpdFactory>( new HttpdFactory( p_access ) ),
        hints
    });
    if ( !p_sys->provider ) {
        msg_Err( p_access, "Failed to load the given provider" );
        return VLC_EGENERIC;
    }

    msg_Dbg( p_access, "Path: %s", p_sys->url.psz_path );
    // Obtain the object from the path requested.
    auto result = WrapLibcloudstorageFunct( p_access,
            &ICloudProvider::getItemAsync, *p_sys->provider,
            vlc_uri_decode( p_sys->url.psz_path ) );
    if ( result == nullptr )
    {
        msg_Err( p_access, "Failed to find %s in the provider %s",
                 p_sys->url.psz_path, p_sys->url.psz_host );
        return VLC_EGENERIC;
    }
    p_sys->current_item = result;
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

    char* url_encoded = vlc_uri_encode( item->filename().c_str() );
    url << url_encoded;
    free( url_encoded );

    i_type = item->type() == IItem::FileType::Directory ?
        ITEM_TYPE_DIRECTORY : ITEM_TYPE_FILE;

    return access_fsdir_additem( p_fsdir, url.str().c_str(),
            item->filename().c_str(), i_type, ITEM_NET );
}

static int ReadDir( stream_t *p_access, input_item_node_t *p_node )
{
    access_sys_t *p_sys = (access_sys_t *) p_access->p_sys;
    struct access_fsdir fsdir;

    // Select the correct function because there 2 functions with same name,
    // they were overloaded. So they must be selected by their parameters types
    auto fn = static_cast<ICloudProvider::ListDirectoryRequest::Pointer
        (ICloudProvider::*)(IItem::Pointer,cloudstorage::ListDirectoryCallback)>
            (&ICloudProvider::listDirectoryAsync);
    // Request libcloudstorage to list directory
    auto result = WrapLibcloudstorageFunct( p_access, fn, *(p_sys->provider),
                                            p_sys->current_item );
    if ( result == nullptr )
        return VLC_EGENERIC;

    // Insert the items from the result of the request
    access_fsdir_init( &fsdir, p_access, p_node );
    int error_code = VLC_SUCCESS;
    for ( auto &i : *result )
    {
        if ( AddItem( &fsdir, p_access, i ) != VLC_SUCCESS )
        {
            error_code = VLC_EGENERIC;
            break;
        }
    }
    access_fsdir_finish( &fsdir, error_code == VLC_SUCCESS );
    return error_code;
}

static std::string ReadFile( const std::string& filename )
{
    char * base_path = config_GetDataDir();

    std::string data_filename;
    data_filename.append( base_path );
    free( base_path );
    data_filename.append( DIR_SEP );
    data_filename.append( filename );
    std::ifstream stream( data_filename, std::ios::in | std::ios::binary );
    if ( !stream )
        return "";

    std::string contents;
    stream.seekg( 0, std::ios::end );
    contents.resize( stream.tellg() );
    stream.seekg( 0, std::ios::beg );
    stream.read( &contents[0], contents.size() );
    stream.close();

    return contents;
}
