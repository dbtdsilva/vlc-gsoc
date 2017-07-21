/*****************************************************************************
 * provider_httpd.cpp: Inherit class IHttpd from libcloudstorage
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

#include "provider_httpd.h"

#include <vlc_url.h>
#include <sstream>
#include <map>

int Httpd::httpRequestCallback( httpd_callback_sys_t * args,
        httpd_client_t * client, httpd_message_t * answer,
        const httpd_message_t * query )
{
    
}
Httpd::Response::Response(int code, const IResponse::Headers& headers,
                           const std::string& body)
{
}

Httpd::Response::~Response()
{
}

void Httpd::Response::send(const IConnection& c)
{
}

Httpd::CallbackResponse::CallbackResponse(
    int code, const IResponse::Headers& headers, int size, int chunk_size,
    IResponse::ICallback::Pointer callback)
{
}

Httpd::Connection::Connection(const char* url) : c_url(url) {}

const char* Httpd::Connection::getParameter(
    const std::string& name) const
{
}

std::string Httpd::Connection::url() const
{
    return c_url;
}

Httpd::Httpd( IHttpServer::ICallback::Pointer cb, IHttpServer::Type type,
             int port, access_t * access ) :
      p_callback(cb)
{
    (void) type;
    
    int default_port = var_GetInteger( access->obj.libvlc, "http-port" );
    var_SetInteger( access->obj.libvlc, "http-port", port );
    host = vlc_http_HostNew( VLC_OBJECT( access ) );
    var_SetInteger( access->obj.libvlc, "http-port", default_port );
    
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
    if ( host != nullptr )
    {
        if ( url_root != nullptr )
            httpd_UrlDelete( url_root );
        if ( url_login != nullptr )
            httpd_UrlDelete( url_login );
        httpd_HostDelete ( host );
    }
}

Httpd::IResponse::Pointer Httpd::createResponse(int code, 
        const IResponse::Headers& headers, const std::string& body) const
{
    return std::make_unique<Response>(code, headers, body);
}

Httpd::IResponse::Pointer Httpd::createResponse(int code,
        const IResponse::Headers& headers, int size, int chunk_size,
        IResponse::ICallback::Pointer cb) const
{
    return std::make_unique<CallbackResponse>(code, headers, size, chunk_size,
            std::move(cb));
}

HttpdFactory::HttpdFactory( access_t* access ) : p_access( access ) {}

IHttpServer::Pointer HttpdFactory::create(
        IHttpServer::ICallback::Pointer cb, const std::string&,
        IHttpServer::Type type, int port)
{
    return std::make_unique<Httpd>(cb, type, port, p_access);
}