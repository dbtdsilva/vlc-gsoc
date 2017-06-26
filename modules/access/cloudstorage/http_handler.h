/*****************************************************************************
 * file.h: HTTP file handler
 *****************************************************************************
 * Copyright (C) 2017
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

#include <stdint.h>

/**
 * \defgroup http_file Files
 * HTTP read-only files
 * \ingroup http_res
 * @{
 */

struct vlc_http_mgr;
struct vlc_http_resource;

/**
 * Creates an HTTP handler.
 *
 * Allocates a structure for a remote HTTP-served read-only file.
 *
 * @param url URL of the file to read
 * @param ua user agent string (or NULL to ignore)
 * @param ref referral URL (or NULL to ignore)
 *
 * @return an HTTP resource object pointer, or NULL on error
 */
struct vlc_http_resource *vlc_http_handler_create(struct vlc_http_mgr *mgr,
                                               const char *url, const char *ua,
                                               const char *ref,
                                               const char *method);

#define vlc_http_file_get_status vlc_http_res_get_status
#define vlc_http_file_get_redirect vlc_http_res_get_redirect
#define vlc_http_file_get_type vlc_http_res_get_type
#define vlc_http_file_destroy vlc_http_res_destroy

/** @} */
