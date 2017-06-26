/*****************************************************************************
 * file.c: HTTP read-only file
 *****************************************************************************
 * Copyright (C) 2015 2017
 *
 * Authors: Diogo Silva <dbtdsilva@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "http_handler.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vlc_common.h>
#include <vlc_block.h>
#include <vlc_strings.h>

#include "access/http/conn.h"
#include "access/http/connmgr.h"
#include "access/http/resource.h"
#include "access/http/message.h"

#pragma GCC visibility push(default)

static int vlc_http_handler_req(const struct vlc_http_resource *res,
                             struct vlc_http_msg *req, void *opaque)
{
    return 0;
}

static int vlc_http_handler_resp(const struct vlc_http_resource *res,
                              const struct vlc_http_msg *resp, void *opaque)
{
    return 0;
}

static const struct vlc_http_resource_cbs vlc_http_handler_callbacks =
{
    vlc_http_handler_req,
    vlc_http_handler_resp,
};

struct vlc_http_resource *vlc_http_handler_create(struct vlc_http_mgr *mgr,
                                               const char *uri, const char *ua,
                                               const char *ref,
                                               const char *method)
{
    struct vlc_http_resource resource;

    if (vlc_http_res_init(&resource, &vlc_http_handler_callbacks,
                          mgr, uri, ua, ref, method))
    {
        return NULL;
    }

    return &resource;
}