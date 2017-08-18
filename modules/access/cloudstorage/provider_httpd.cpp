/*****************************************************************************
 * provider_httpd.cpp: Inherit class IHttpd from libcloudstorage
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

#include "provider_httpd.h"

#include <vlc_url.h>
#include <sstream>
#include <map>

int Httpd::httpRequestCallback( httpd_callback_sys_t * cls,
        httpd_client_t * client, httpd_message_t * answer,
        const httpd_message_t * query )
{
    (void) client;
    Httpd* server = (Httpd *) cls;

    // Pre-fill data to be sent to the callback
    std::unordered_map<std::string, std::string> args, headers;
    std::string argument;
    if ( query->psz_args != nullptr )
    {
        std::istringstream iss( std::string(
            vlc_uri_decode( (char*) query->psz_args ) ) );
        while ( std::getline( iss, argument, '&' ) ) {
            size_t equal_div = argument.find('=');
            if ( equal_div == std::string::npos )
                continue;

            std::string key = argument.substr( 0, equal_div );
            std::string value = argument.substr( equal_div + 1 );
            args.insert( std::make_pair( key, value ) );
        }
    }
    for (unsigned int i = 0; i < query->i_headers; i++) {
        headers.insert( std::make_pair(
            query->p_headers[i].name, query->p_headers[i].value ) );
    }
    Httpd::Connection connection( query->psz_url, args, headers );

    // Inform about a received connection in order to get the proper response
    auto response_ptr = server->callback()->
        receivedConnection( *server, &connection );
    auto response = static_cast<Httpd::Response*>( response_ptr.get() );

    // Fill the data to be requests to the lib
    answer->i_proto = HTTPD_PROTO_HTTP;
    answer->i_version= 1;
    answer->i_type = HTTPD_MSG_ANSWER;
    answer->i_status = response->getCode();

    IResponse::Headers r_headers = response->getHeaders();
    answer->i_headers = r_headers.size();
    for ( auto header : r_headers )
        httpd_MsgAdd( answer,
                header.first.c_str(), "%s", header.second.c_str() );
    answer->i_body = response->getBody().length();
    char * message = strdup( response->getBody().c_str() );
    answer->p_body = (unsigned char *) message;

    httpd_MsgAdd( answer, "Content-Length", "%d", answer->i_body );

    return VLC_SUCCESS;
}

Httpd::Response::Response( int code, const IResponse::Headers& headers,
                           const std::string& body ) :
    i_code( code ), m_headers( headers ), p_body( body ) {}

Httpd::CallbackResponse::CallbackResponse( int code,
        const IResponse::Headers& headers, int size, int chunk_size,
        IResponse::ICallback::Pointer ptr )
{
    // Not required for authorization purposes
    (void) code; (void) headers; (void) size; (void) chunk_size; (void) ptr;
}

Httpd::Connection::Connection( const char* url,
        const std::unordered_map<std::string, std::string>& args,
        const std::unordered_map<std::string, std::string>& headers ) :
    c_url( url ), m_args( args ), m_headers( headers ) {}

const char* Httpd::Connection::getParameter(
    const std::string& name) const
{
    auto element = m_args.find(name);
    return element == m_args.end() ? nullptr : element->second.c_str();
}

const char* Httpd::Connection::header(const std::string& name) const
{
    auto element = m_headers.find(name);
    return element == m_headers.end() ? nullptr : element->second.c_str();
}

std::string Httpd::Connection::url() const
{
    return c_url;
}

void Httpd::Connection::onCompleted(CompletedCallback f)
{
    ptr_callback = f;
}

void Httpd::Connection::suspend()
{
    // Not required for authorization purposes
}

void Httpd::Connection::resume()
{
    // Not required for authorization purposes
}

Httpd::Httpd( IHttpServer::ICallback::Pointer cb, IHttpServer::Type type,
              stream_t * access ) :
      p_callback( cb ), host( nullptr ), url_root( nullptr ),
      url_login( nullptr )
{
    // Prevent the usage of this server for other effects than authorization
    if ( type != IHttpServer::Type::Authorization )
    {
        throw std::runtime_error("Provider used for this purpose is not "
                "support since this http server should only be used for "
                "authorization purposes");
    }

    host = vlc_http_HostNew( VLC_OBJECT( access ) );

    // Exception is handled by HttpdFactory right away
    if ( host == nullptr )
        throw std::runtime_error("Failed to create host");

    // Spawns two URLs that might receive requests
    url_root = httpd_UrlNew( host, "/auth", NULL, NULL );
    url_login = httpd_UrlNew( host, "/auth/login", NULL, NULL );

    if ( url_root != nullptr )
    {
        httpd_UrlCatch( url_root, HTTPD_MSG_HEAD, httpRequestCallback,
                (httpd_callback_sys_t*) this );
        httpd_UrlCatch( url_root, HTTPD_MSG_GET, httpRequestCallback,
                (httpd_callback_sys_t*) this );
        httpd_UrlCatch( url_root, HTTPD_MSG_POST, httpRequestCallback,
                (httpd_callback_sys_t*) this );
    }

    if ( url_login != nullptr )
    {
        httpd_UrlCatch( url_login, HTTPD_MSG_HEAD, httpRequestCallback,
                (httpd_callback_sys_t*) this );
        httpd_UrlCatch( url_login, HTTPD_MSG_GET, httpRequestCallback,
                (httpd_callback_sys_t*) this );
        httpd_UrlCatch( url_login, HTTPD_MSG_POST, httpRequestCallback,
                (httpd_callback_sys_t*) this );
    }
}

Httpd::~Httpd()
{
    if ( url_root != nullptr )
        httpd_UrlDelete( url_root );
    if ( url_login != nullptr )
        httpd_UrlDelete( url_login );
    if ( host != nullptr )
        httpd_HostDelete ( host );
}

Httpd::IResponse::Pointer Httpd::createResponse( int code,
        const IResponse::Headers& headers, const std::string& body ) const
{
    return std::make_unique<Response>(code, headers, body);
}

Httpd::IResponse::Pointer Httpd::createResponse( int code,
        const IResponse::Headers& headers, int size, int chunk_size,
        IResponse::ICallback::Pointer ptr ) const
{
    return std::make_unique<CallbackResponse>( code, headers, size, chunk_size,
            std::move( ptr ) );
}

HttpdFactory::HttpdFactory( stream_t* access ) : p_access( access ) {}

IHttpServer::Pointer HttpdFactory::create(
        IHttpServer::ICallback::Pointer cb, const std::string&,
        IHttpServer::Type type )
{
    IHttpServer::Pointer ptr;
    try
    {
        ptr = std::make_unique<Httpd>( cb, type, p_access );
    }
    catch ( const std::runtime_error& exception )
    {
        msg_Err( p_access, "Failed to create Http server: %s",
                 exception.what() );
        ptr = nullptr;
    }
    return ptr;
}
