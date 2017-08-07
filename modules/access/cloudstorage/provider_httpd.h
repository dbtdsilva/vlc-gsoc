/*****************************************************************************
 * provider_httpd.h: Inherit class IHttpServer from libcloudstorage
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
 * You should have receivedaaaw a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifndef VLC_CLOUDSTORAGE_PROVIDER_HTTPD_H
#define VLC_CLOUDSTORAGE_PROVIDER_HTTPD_H

#include "access.h"

#include <string>
#include <unordered_map>
#include <vlc_httpd.h>

using cloudstorage::IHttpServer;
using cloudstorage::IHttpServerFactory;

class Httpd : public IHttpServer
{
public:
    Httpd( IHttpServer::ICallback::Pointer cb, IHttpServer::Type type,
           stream_t * access );
    ~Httpd();

    class Response : public IResponse {
    public:
        Response() = default;
        Response(int code, const IResponse::Headers&, const std::string& body);

        int getCode() { return i_code; }
        IResponse::Headers getHeaders() { return m_headers; }
        const std::string& getBody() { return p_body; }
    private:
        int i_code;
        IResponse::Headers m_headers;
        const std::string p_body;
    };

    class CallbackResponse : public Response {
    public:
        CallbackResponse(int code, const IResponse::Headers&, int size,
                int chunk_size, IResponse::ICallback::Pointer);
    };

    class Connection : public IConnection {
    public:
        Connection(const char* url,
                const std::unordered_map<std::string, std::string> args,
                const std::unordered_map<std::string, std::string> headers);
        const char* getParameter(const std::string& name) const override;
        const char* header(const std::string& name) const override;
        std::string url() const override;
        void onCompleted(CompletedCallback) override;
        void suspend() override;
        void resume() override;

    private:
        std::string c_url;
        CompletedCallback ptr_callback;
        const std::unordered_map<std::string, std::string> m_args;
        const std::unordered_map<std::string, std::string> m_headers;
    };

    IResponse::Pointer createResponse(int code, const IResponse::Headers&,
                                const std::string& body) const override;
    IResponse::Pointer createResponse(int code, const IResponse::Headers&,
                                int size, int chunk_size,
                                IResponse::ICallback::Pointer) const override;
    ICallback::Pointer callback() const { return p_callback; }
private:
    static int httpRequestCallback( httpd_callback_sys_t * args,
            httpd_client_t *, httpd_message_t * answer,
            const httpd_message_t * query_t );

    ICallback::Pointer p_callback;
    httpd_host_t *host;
    httpd_url_t *url_root, *url_login;
    httpd_stream_t *file_stream;
};

class HttpdFactory : public IHttpServerFactory
{
public:
    HttpdFactory( stream_t* access );
    IHttpServer::Pointer create(IHttpServer::ICallback::Pointer,
                              const std::string& session_id,
                              IHttpServer::Type) override;
private:
    stream_t* p_access;
};

#endif