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

// Initializing static members
const struct vlc_http_resource_cbs HttpRequest::handler_callbacks =
        { httpRequestHandler, httpResponseHandler };

// HttpRequest interface implementation
struct HttpRequestData {
    const HttpRequest *ptr;
};

HttpRequest::HttpRequest( access_t* access, const std::string& url,
        const std::string& method, bool follow_redirect ) :
    p_access( access ), req_url( url ), req_method( method ),
    req_follow_redirect( follow_redirect ),
    manager( vlc_http_mgr_create( VLC_OBJECT(p_access), NULL ))
{
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

    struct vlc_http_resource resource;
    // Concatenating the URL in the parameters
    std::unordered_map<std::string, std::string>::const_iterator it;
    for ( it = req_parameters.begin(); it != req_parameters.end(); it++ )
    {
        if (it != req_parameters.begin())
            params_url += "&";
        params_url += it->first + "=" + it->second;
    }
    std::string url = req_url + (!params_url.empty() ? ("?" + params_url) : "");

    if ( vlc_http_res_init(&resource, &handler_callbacks, manager, url.c_str(),
                          NULL, NULL, req_method.c_str()) )
        return 500;

    if ( resource.response == NULL && resource.failure )
        return 500;

    HttpRequestData *callback_data = new HttpRequestData();
    callback_data->ptr = this;
    resource.response = vlc_http_res_open(&resource, callback_data);
    if ( resource.response == NULL )
    {
        resource.failure = true;
        return 500;
    }

    struct block_t* block = vlc_http_file_read(&resource);
    std::string response_msg = "";
    while (block != NULL)
    {
        response_msg += std::string((char*) block->p_buffer, block->i_buffer);
        block = block->p_next;
    }
    response.write(response_msg.c_str(), response_msg.size());

    response_code = vlc_http_msg_get_status(resource.response);

    // Invoke the respective callbacks
    cb->receivedHttpCode(static_cast<int>(response_code));
    cb->receivedContentLength(static_cast<int>(response_msg.size()));

    return response_code;
}

int HttpRequest::httpRequestHandler(const struct vlc_http_resource *res,
                             struct vlc_http_msg *req, void *opaque)
{
    (void) res;

    HttpRequestData* data = static_cast<HttpRequestData *>( opaque );
    for ( const auto& header : data->ptr->headerParameters() )
    {
        // Prevent duplicate entries and host header is controlled by
        // http_file_create
        if ( vlc_http_msg_get_header(req, header.first.c_str()) == NULL &&
                strcasecmp(header.first.c_str(), "Host"))
        {
            vlc_http_msg_add_header(req, header.first.c_str(), "%s",
                header.second.c_str());
        }
    }
    return 0;
}

int HttpRequest::httpResponseHandler(const struct vlc_http_resource *res,
                             const struct vlc_http_msg *resp, void *opaque)
{
    (void) res;
    (void) resp;
    (void) opaque;
    fprintf(stderr, "Response received\n");
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
    return Json::valueToQuotedString(value.c_str());
}

std::string Http::error( int error ) const
{
    return "Error code " + std::to_string(error);
}