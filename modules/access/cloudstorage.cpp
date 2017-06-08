/*****************************************************************************
 * cloudstorage.cpp: cloud access module using libcloudstorage for VLC
 *****************************************************************************
 * Copyright (C) 2017 William Ung
 *
 * Authors: William Ung <williamung@msn.com>
 *          Diogo Silva <dbtdsilva@gmail.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <vlc_plugin.h>

#include "cloudstorage/access.h"
#include "cloudstorage/services_discovery.h"

static int vlc_sd_probe_Open( vlc_object_t * );
VLC_SD_PROBE_HELPER( "cloudstorage", N_("Cloud Storage"), SD_CAT_INTERNET )

vlc_module_begin()
    set_shortname(N_("cloudstorage"))
    set_capability("access", 0)
    set_description(N_("cloudstorage input"))
    set_category(CAT_INPUT)
    set_subcategory(SUBCAT_INPUT_ACCESS)
    // callbacks are defined under cloudstorage/access.h
    set_callbacks(Open, Close)

    add_submodule()
        set_description( N_("Cloudstorage Services Discovery") )
        set_category( CAT_PLAYLIST )
        set_subcategory( SUBCAT_PLAYLIST_SD )
        set_capability( "services_discovery", 0 )
        // callbacks are defined under cloudstorage/services_discovery.h
        set_callbacks( SDOpen, SDClose )

        VLC_SD_PROBE_SUBMODULE
vlc_module_end()
