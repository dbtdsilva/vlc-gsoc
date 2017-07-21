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

Httpd::Httpd(IHttpServer::ICallback::Pointer cb, IHttpServer::Type type,
             int port) :
      p_callback(cb)
{
}

Httpd::~Httpd()
{
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
    return std::make_unique<Httpd>(cb, type, port);
}