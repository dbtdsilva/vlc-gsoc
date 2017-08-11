/*****************************************************************************
 * provider_http.cpp: Inherit class IHttp from libcloudstorage
 *****************************************************************************
 * Copyright (C) 2017 VideoLabs and VideoLAN
 *
 * Authors: Diogo Silva <dbtdsilva@gmail.com>
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

#include "provider_http.h"

#include <memory>
#include <vlc_common.h>
#include <vlc_url.h>
#include <vlc_block.h>
#include <json/json.h>
#include <iterator>

extern "C" {
#include "access/http/resource.h"
#include "access/http/connmgr.h"
#include "access/http/message.h"
}

#include "provider_http_stream.h"

#define URL_REDIRECT_LIMIT  10

// Initializing static members
const struct vlc_http_resource_cbs HttpRequest::handler_callbacks =
        { httpRequestHandler, httpResponseHandler };

// HttpRequest interface implementation
struct HttpRequestData {
    const HttpRequest *ptr;
    std::istream *data;
};

HttpRequest::HttpRequest( stream_t* access, const std::string& url,
        const std::string& method, bool follow_redirect ) :
    p_access( access ), req_url( url ), req_method( method ),
    req_follow_redirect( follow_redirect ),
    manager( vlc_http_mgr_create( VLC_OBJECT(p_access), NULL ))
{
}

HttpRequest::~HttpRequest()
{
    if ( manager != NULL )
        vlc_http_mgr_destroy( manager );
}

void HttpRequest::setParameter( const std::string& parameter,
        const std::string& value )
{
    req_parameters[parameter] = value;
}

void HttpRequest::setHeaderParameter( const std::string& parameter,
        const std::string& value )
{
    req_header_parameters[parameter] = value;
}

const std::unordered_map<std::string, std::string>&
    HttpRequest::parameters() const
{
    return req_parameters;
}

const std::unordered_map<std::string, std::string>&
    HttpRequest::headerParameters() const
{
    return req_header_parameters;
}

const std::string& HttpRequest::url() const
{
    return req_url;
}

const std::string& HttpRequest::method() const
{
    return req_method;
}

bool HttpRequest::follow_redirect() const
{
    return req_follow_redirect;
}

void HttpRequest::send( CompleteCallback on_completed,
        std::shared_ptr<std::istream> data,
        std::shared_ptr<std::ostream> response,
        std::shared_ptr<std::ostream> error_stream,
        ICallback::Pointer cb ) const
{
    std::string current_url = req_url;
    int redirect_counter = 0;

request:
    struct vlc_http_resource *resource =
        (struct vlc_http_resource *) malloc( sizeof( *resource ) );
    if ( resource == NULL )
        return;

    // Concatenating the URL in the parameters
    std::string params_url;
    std::unordered_map<std::string, std::string>::const_iterator it;
    for ( it = req_parameters.begin(); it != req_parameters.end(); ++it )
    {
        if ( it != req_parameters.begin() )
            params_url += "&";
        params_url += it->first + "=" + it->second;
    }
    std::string url = current_url + (!params_url.empty() ?
                      ("?" + params_url) : "");

    // Init the resource
    // Fixup in order to fix the raw URL (not-encoded parts) from the
    // libcloudstorage. Either way, its safer to prevent.
    char *fixed_url = vlc_uri_fixup( url.c_str() );
    int res = vlc_http_res_init( resource, &handler_callbacks, manager,
                                 fixed_url, NULL, NULL, req_method.c_str() );
    free(fixed_url);
    if ( res != VLC_SUCCESS )
    {
        vlc_http_res_destroy( resource );
        return;
    }

    // Invoke the HTTP request (GET, POST, ..)
    HttpRequestData *callback_data = new HttpRequestData();
    if ( callback_data == NULL )
    {
        vlc_http_res_destroy( resource );
        return;
    }
    callback_data->ptr = this;
    callback_data->data = data.get();
    resource->response = vlc_http_res_open( resource, callback_data );
    delete callback_data;

    // Get the response code obtained
    if ( resource->response == NULL )
    {
        msg_Err( p_access, "Failed to obtain a response from the request %s %s",
                 req_method.c_str(), current_url.c_str() );
        vlc_http_res_destroy( resource );
        return;
    }
    int response_code = vlc_http_msg_get_status( resource->response );

    // Get the redirect URI before destroying the resource
    char *redirect_uri = vlc_http_res_get_redirect( resource );
    // Check for redirects if exist
    if ( req_follow_redirect && redirect_uri != NULL &&
            IHttpRequest::isRedirect( response_code ) )
    {
        redirect_counter += 1;
        vlc_http_res_destroy( resource );
        if ( redirect_counter > URL_REDIRECT_LIMIT )
        {
            msg_Err( p_access, "URL has been redirected too many times.");
            return;
        }
        current_url = std::string( redirect_uri );
        goto request;
    }

    // Read the payload response into the buffer (ostream)
    unsigned int content_length = 0;
    struct block_t *block = vlc_http_res_read( resource );
    while (block != NULL)
    {
        if ( IHttpRequest::isSuccess( response_code ) )
            response->write( (char *) block->p_buffer, block->i_buffer );
        else
            error_stream->write( (char *) block->p_buffer, block->i_buffer );
        content_length += block->i_buffer;

        block_Release(block);
        block = vlc_http_res_read( resource );
    }

    // Debug messages about the output
    if ( IHttpRequest::isSuccess( response_code ) )
        msg_Dbg( p_access, "%s %s succeed with a code %d", req_method.c_str(),
                current_url.c_str(), response_code );
    else
        msg_Err( p_access, "Failed to request %s %s with an error code of %d",
                req_method.c_str(), current_url.c_str(), response_code );

    // Invoke the respective callbacks
    cb->receivedHttpCode( static_cast<int>( response_code ) );
    cb->receivedContentLength( static_cast<int>( content_length ) );

    vlc_http_res_destroy( resource );

    on_completed( response_code, response, error_stream );
}

int HttpRequest::httpRequestHandler( const struct vlc_http_resource *res,
                             struct vlc_http_msg *req, void *opaque )
{
    (void) res;

    // Inserting headers
    HttpRequestData* data = static_cast<HttpRequestData *>( opaque );
    for ( const auto& header : data->ptr->headerParameters() )
    {
        // Prevent duplicate entries and host header is controlled by
        // http_file_create
        if ( vlc_http_msg_get_header( req, header.first.c_str() ) == NULL &&
                strcasecmp( header.first.c_str(), "Host" ) )
        {
            vlc_http_msg_add_header( req, header.first.c_str(), "%s",
                header.second.c_str() );
        }
    }

    // Pre-calculate the content-length of the stream (seekable)
    std::streampos pos = data->data->tellg();
    data->data->seekg( 0, std::ios::end );
    std::streamsize content_length = data->data->tellg() - pos;
    data->data->seekg( pos );

    // Stream the data using VLC format (into block_t)
    vlc_http_stream *stream = vlc_payload_stream_open( data->data );
    vlc_http_msg_attach( req, stream );

    // Content-Length is a mandatory field sometimes (e.g PUT methods)
    if ( data->ptr->req_method != "GET" )
    {
        vlc_http_msg_add_header( req, "Content-Length", "%ld", content_length );
        if ( vlc_http_msg_get_header( req, "Content-Type" ) == NULL &&
             data->ptr->req_method != "PUT" && content_length > 0)
        {
            vlc_http_msg_add_header( req, "Content-Type", "%s",
                    "application/x-www-form-urlencoded" );
        }
    }
    return 0;
}

int HttpRequest::httpResponseHandler( const struct vlc_http_resource *res,
                             const struct vlc_http_msg *resp, void *opaque )
{
    (void) res;
    (void) resp;
    (void) opaque;
    return 0;
}

// Http interface implementation
Http::Http( stream_t *access ) : p_access( access )
{
}

cloudstorage::IHttpRequest::Pointer Http::create( const std::string& url,
            const std::string& method, bool follow_redirect ) const
{
    return std::make_unique<HttpRequest>( p_access, url, method,
            follow_redirect );
}
