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

// Http interface implementation
cloudstorage::IHttpRequest::Pointer Http::create(const std::string&,
            const std::string&, bool) const {
    return nullptr;
}

std::string Http::unescape(const std::string&) const {
    return "";
}

std::string Http::escape(const std::string&) const {
    return "";
}

std::string Http::escapeHeader(const std::string&) const {
    return "";
}

std::string Http::error(int) const {
    return "";
}

// HttpRequest interface implementation
HttpRequest::HttpRequest(const std::string& url, const std::string& method,
        bool follow_redirect) {
}

void HttpRequest::setParameter(const std::string& parameter,
        const std::string& value) {

}

void HttpRequest::setHeaderParameter(const std::string& parameter,
        const std::string& value) {
}

const std::unordered_map<std::string, std::string>& HttpRequest::parameters()
        const {
    return req_parameters;
}

const std::unordered_map<std::string, std::string>& HttpRequest::headerParameters()
        const {
    return req_header_parameters;
}

const std::string& HttpRequest::url() const {
    return req_url;
}

const std::string& HttpRequest::method() const {
    return req_method;
}

bool HttpRequest::follow_redirect() const {
    return req_follow_redirect;
}

int HttpRequest::send(std::istream& data, std::ostream& response,
        std::ostream* error_stream, ICallback::Pointer) const {
    return 0;
}
