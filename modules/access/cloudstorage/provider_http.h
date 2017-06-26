/*****************************************************************************
 * provider_http.h: Inherit class IHttp from libcloudstorage
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

#ifndef VLC_CLOUDSTORAGE_PROVIDER_HTTP_H
#define VLC_CLOUDSTORAGE_PROVIDER_HTTP_H

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <IHttp.h>
#include <string>
#include <unordered_map>

#include "access.h"

class Http : public cloudstorage::IHttp
{
public:
    Http( access_t *access );
    cloudstorage::IHttpRequest::Pointer create( const std::string&,
            const std::string&, bool ) const override;
    std::string unescape( const std::string& ) const override;
    std::string escape( const std::string& ) const override;
    std::string escapeHeader( const std::string& ) const override;
    std::string error( int ) const override;

private:
    access_t *p_access;
};

class HttpRequest : public cloudstorage::IHttpRequest
{
public:
    HttpRequest( access_t* access, const std::string& url,
            const std::string& method, bool follow_redirect );

    void setParameter( const std::string& parameter,
            const std::string& value ) override;
    void setHeaderParameter( const std::string& parameter,
            const std::string& value ) override;

    const std::unordered_map<std::string, std::string>& parameters()
            const override;
    const std::unordered_map<std::string, std::string>& headerParameters()
            const override;

    const std::string& url() const override;
    const std::string& method() const override;
    bool follow_redirect() const override;

    int send( std::istream& data, std::ostream& response,
            std::ostream* error_stream = nullptr,
            ICallback::Pointer = nullptr ) const override;

private:
    static int httpRequestHandler(const struct vlc_http_resource *res,
                             struct vlc_http_msg *req, void *opaque);
    static int httpResponseHandler(const struct vlc_http_resource *res,
                             const struct vlc_http_msg *resp, void *opaque);

    access_t *p_access;
    std::unordered_map<std::string, std::string> req_parameters;
    std::unordered_map<std::string, std::string> req_header_parameters;
    std::string req_url, req_method;
    bool req_follow_redirect;
    struct vlc_http_mgr *manager;
    static const struct vlc_http_resource_cbs handler_callbacks;
};

#endif /* VLC_CLOUDSTORAGE_PROVIDER_HTTP_H */

