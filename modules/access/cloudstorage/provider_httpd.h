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

using cloudstorage::IHttpd;

class Httpd : public IHttpd {
public:
    Httpd( access_t *access );
    void startServer(uint16_t port, CallbackFunction request_callback,
            void* data) override;
    void stopServer() override;
private:
    static int internalCallback( httpd_callback_sys_t * args, httpd_client_t *,
            httpd_message_t * answer, const httpd_message_t * query_t );
    access_t *p_access;
    access_sys_t *p_sys;
    httpd_host_t *host;
    httpd_url_t *url_root, *url_login;

    struct CallbackData {
        CallbackFunction request_callback;
        void* custom_data;
    };
};

#endif