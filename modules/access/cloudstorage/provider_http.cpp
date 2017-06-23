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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <memory>
#include <vlc_common.h>
#include <vlc_url.h>

#include "../http/file.h"
#include "../http/conn.h"
#include "../http/connmgr.h"
#include "../http/resource.h"
#include "../http/message.h"

// Http interface implementation
cloudstorage::IHttpRequest::Pointer Http::create( const std::string& url,
            const std::string& method, bool follow_redirect ) const
{
    return std::make_unique<HttpRequest>( url, method, follow_redirect );
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
    return value;
}

std::string Http::error( int error ) const
{
    return "";
}

// HttpRequest interface implementation
HttpRequest::HttpRequest( const std::string& url, const std::string& method,
        bool follow_redirect ) : req_url( url ), req_method( method ),
                                 req_follow_redirect( follow_redirect )
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
    return 0;
}
