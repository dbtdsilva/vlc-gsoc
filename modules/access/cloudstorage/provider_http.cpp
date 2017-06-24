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
#include <vlc_block.h>
#include <json/json.h>

#include "access/http/file.h"
#include "access/http/conn.h"
#include "access/http/connmgr.h"
#include "access/http/resource.h"
#include "access/http/message.h"

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

// HttpRequest interface implementation
HttpRequest::HttpRequest( access_t* access, const std::string& url,
        const std::string& method, bool follow_redirect ) :
    p_access( access ), req_url( url ), req_method( method ),
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
    std::shared_ptr<ICallback> callback( std::move(cb) );

    std::string params_url;
    // Create URL with parameters
    std::unordered_map<std::string, std::string>::const_iterator it;
    for ( it = req_parameters.begin(); it != req_parameters.end(); it++ )
    {
        if (it != req_parameters.begin())
            params_url += "&";
        params_url += it->first + "=" + it->second;
    }
    std::string url = req_url + (!params_url.empty() ? ("?" + params_url) : "");
    // Initializing the request
    struct vlc_http_resource *res;
    struct vlc_http_mgr *manager;
    manager = vlc_http_mgr_create( VLC_OBJECT(p_access), NULL );
    if (manager == NULL) {
        return 500;
    }
    res = vlc_http_file_create(manager, url.c_str(), NULL,
            NULL, req_method.c_str());
    if (res == NULL) {
        return 500;
    }

    int status;
    struct vlc_http_msg *resp;
    if (res->response == NULL)
    {
        if (res->failure)
            return 500;

        void* opaque = res + 1;
        struct vlc_http_msg *req;
retry:
        req = vlc_http_res_req(res, opaque);
        if (unlikely(req == NULL)) {
            res->response = NULL;
            goto end;
        }
        resp = vlc_http_mgr_request(res->manager, res->secure,
                                    res->host, res->port, req);
        vlc_http_msg_destroy(req);

        resp = vlc_http_msg_get_final(resp);
        if (resp == NULL) {
            res->response = NULL;
            goto end;
        }

        for ( const auto& header : req_header_parameters )
            vlc_http_msg_add_header(req, header.first.c_str(), "%s",
                    header.second.c_str());

        vlc_http_msg_get_cookies(resp, vlc_http_mgr_get_jar(res->manager),
                                 res->host, res->path);
        status = vlc_http_msg_get_status(resp);
        if (status < 200 || status >= 599)
            goto fail;

        if (status == 406 && res->negotiate)
        {
            vlc_http_msg_destroy(resp);
            res->negotiate = false;
            goto retry;
        }

        if (res->cbs->response_validate(res, resp, opaque))
            goto fail;

        res->response = resp;
        goto end;
fail:
        vlc_http_msg_destroy(resp);
        return 500;
end:
        if (res->response == NULL)
        {
            res->failure = true;
            return -1;
        }
    }

    struct block_t* block = vlc_http_file_read(res);

    std::string response_msg = "";
    while (block != NULL)
    {
        response_msg += std::string((char*) block->p_buffer, block->i_buffer);
        block = block->p_next;
    }
    response.write(response_msg.c_str(), response_msg.size());


    int response_code = vlc_http_msg_get_status(res->response);
    callback->receivedHttpCode(static_cast<int>(response_code));
    callback->receivedContentLength(static_cast<int>(response_msg.size()));

    return response_code;
}
