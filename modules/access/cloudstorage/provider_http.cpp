/*****************************************************************************
 * provider_http.cpp: Inherit class IHttp from libcloudstorage
 *****************************************************************************
 * Copyright (C) 2017
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

#include "access/http/resource.h"
#include "access/http/connmgr.h"
#include "access/http/message.h"

#define URL_REDIRECT_LIMIT  10

// Initializing static members
const struct vlc_http_resource_cbs HttpRequest::handler_callbacks =
        { httpRequestHandler, httpResponseHandler };

// HttpRequest interface implementation
struct HttpRequestData {
    const HttpRequest *ptr;
    std::istream *data;
};

HttpRequest::HttpRequest( access_t* access, const std::string& url,
        const std::string& method, bool follow_redirect ) :
    p_access( access ), req_url( url ), req_method( method ),
    req_follow_redirect( follow_redirect ),
    manager( vlc_http_mgr_create( VLC_OBJECT(p_access), NULL ))
{
}

HttpRequest::~HttpRequest()
{
    if ( manager != NULL )
        vlc_http_mgr_destroy(manager);
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

int HttpRequest::send( std::istream& data, std::ostream& response,
        std::ostream* error_stream, ICallback::Pointer cb ) const
{
    int response_code;
    std::string params_url;
    std::string current_url = req_url;
    int redirect_counter = 0;

request:
    struct vlc_http_resource *resource =
        (struct vlc_http_resource *) malloc( sizeof( *resource ) );
    if (resource == NULL)
        return -1;

    // Concatenating the URL in the parameters
    std::unordered_map<std::string, std::string>::const_iterator it;
    for ( it = req_parameters.begin(); it != req_parameters.end(); it++ )
    {
        if (it != req_parameters.begin())
            params_url += "&";
        params_url += it->first + "=" + it->second;
    }
    std::string url = current_url + (!params_url.empty() ?
                      ("?" + params_url) : "");

    // Init the resource
    if ( vlc_http_res_init(resource, &handler_callbacks, manager, url.c_str(),
                          NULL, NULL, req_method.c_str()) )
        return -1;

    // Invoke the HTTP request (GET, POST, ..)
    HttpRequestData *callback_data = new HttpRequestData();
    callback_data->ptr = this;
    callback_data->data = &data;

    resource->response = vlc_http_res_open( resource, callback_data );

    delete callback_data;
    if ( resource->response == NULL )
    {
        resource->failure = true;
        return -1;
    }

    // Read the payload response into the buffer (ostream)
    unsigned int content_length = 0;
    struct block_t* block = vlc_http_res_read( resource );
    while (block != NULL)
    {
        response.write( (char *) block->p_buffer, block->i_buffer );
        content_length += block->i_buffer;
        block = vlc_http_res_read( resource );
    }

    // Get the response code obtained
    response_code = vlc_http_msg_get_status( resource->response );
    char *redirect_uri = vlc_http_res_get_redirect( resource );

    vlc_http_res_destroy(resource);

    if ( req_follow_redirect && redirect_uri != NULL &&
            isRedirect(response_code))
    {
        redirect_counter += 1;
        if ( redirect_counter > URL_REDIRECT_LIMIT )
        {
            msg_Err( p_access, "URL has been redirected too many times.");
            return -1;
        }
        current_url = std::string( redirect_uri );
        goto request;
    }

    // Invoke the respective callbacks
    cb->receivedHttpCode( static_cast<int>( response_code ) );
    cb->receivedContentLength( static_cast<int>( content_length ) );

    return response_code;
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

    // Inserting body
    std::string body( std::istreambuf_iterator<char>( *(data->data) ), {} );
    if ( body.size() > 0)
    {
        vlc_http_msg_add_body( req, (uint8_t *) body.c_str(), body.size() );
        if ( vlc_http_msg_get_header( req, "Content-Type" ) == NULL )
            vlc_http_msg_add_header( req, "Content-Type", "%s",
                    "application/x-www-form-urlencoded" );
        vlc_http_msg_add_header( req, "Content-Length", "%s",
                std::to_string( body.size() ).c_str() );
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
Http::Http( access_t *access ) : p_access( access )
{
}

cloudstorage::IHttpRequest::Pointer Http::create( const std::string& url,
            const std::string& method, bool follow_redirect ) const
{
    return std::make_unique<HttpRequest>( p_access, url, method,
            follow_redirect );
}

std::string Http::unescape( const std::string& value ) const
{
    char* decoded_uri = vlc_uri_decode_duplicate( value.c_str() );
    std::string uri_str( decoded_uri );
    free( decoded_uri );
    return uri_str;
}

std::string Http::escape( const std::string& value ) const
{
    char *uri_encoded = vlc_uri_encode( value.c_str() );
    std::string uri_str( uri_encoded );
    free( uri_encoded );
    return uri_str;
}

std::string Http::escapeHeader( const std::string& value ) const
{
    // This will be removed once a implementation for a helper class is done
    // The other escapes are also included
    return Json::valueToQuotedString( value.c_str() );
}

std::string Http::error( int error ) const
{
    return "Error code " + std::to_string( error );
}