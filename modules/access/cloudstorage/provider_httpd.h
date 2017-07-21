/*****************************************************************************
 * provider_httpd.h: Inherit class IHttpd from libcloudstorage
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

#ifndef VLC_CLOUDSTORAGE_PROVIDER_HTTPD_H
#define VLC_CLOUDSTORAGE_PROVIDER_HTTPD_H

#include "access.h"

#include <string>
#include <vlc_httpd.h>

using cloudstorage::IHttpServer;
using cloudstorage::IHttpServerFactory;

class Httpd : public IHttpServer
{
public:
    Httpd(IHttpServer::ICallback::Pointer cb, IHttpServer::Type type, int port);
    ~Httpd();

    class Response : public IResponse {
    public:
        Response() = default;
        Response(int code, const IResponse::Headers&, const std::string& body);
        ~Response();

        void send(const IConnection&) override;
    };

    class CallbackResponse : public Response {
    public:
        CallbackResponse(int code, const IResponse::Headers&, int size,
                int chunk_size, IResponse::ICallback::Pointer);
    };

    class Connection : public IConnection {
    public:
        Connection(const char* url);
        const char* getParameter(const std::string& name) const override;
        std::string url() const override;

    private:
        std::string c_url;
    };

    IResponse::Pointer createResponse(int code, const IResponse::Headers&,
                                const std::string& body) const override;
    IResponse::Pointer createResponse(int code, const IResponse::Headers&,
                                int size, int chunk_size,
                                IResponse::ICallback::Pointer) const override;
    ICallback::Pointer callback() const { return p_callback; }
private:
    ICallback::Pointer p_callback;
};

class HttpdFactory : public IHttpServerFactory
{
public:
    HttpdFactory( access_t* access );
    IHttpServer::Pointer create(IHttpServer::ICallback::Pointer,
                              const std::string& session_id, IHttpServer::Type,
                              int port) override;
private:
    access_t* p_access;
};

#endif