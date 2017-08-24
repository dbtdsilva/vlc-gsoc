/*****************************************************************************
 * provider_callback.h: Inherit class ICallback from libcloudstorage
 *****************************************************************************
 * Copyright (C) 2017 VideoLabs and VideoLAN
 *
 * Authors: Diogo Silva <dbtdsilva@gmail.com>
 *          William Ung <williamung@msn.com>
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

#ifndef VLC_CLOUDSTORAGE_PROVIDER_CALLBACK_H
#define VLC_CLOUDSTORAGE_PROVIDER_CALLBACK_H

#include "access.h"

#include <vlc_dialog.h>

class Callback : public cloudstorage::ICloudProvider::IAuthCallback {
public:
    Callback( stream_t *access ) :
        p_access( access ), p_sys( (access_sys_t *) access->p_sys )
    {}

    Status userConsentRequired( const cloudstorage::ICloudProvider& provider )
                                override
    {
        std::string authorize_url = provider.authorizeLibraryUrl();
        int i_ret = vlc_spawn_browser( p_access, authorize_url.c_str(),
            "Authentication required", "This function requires login through a "
            "website and will open a webbrowser." );
        if ( i_ret != 1 )
        {
            var_SetString( p_access->obj.libvlc, "cloudstorage-auth", "ABORT" );
            return Status::None;
        }
        msg_Info( p_access, "User ConsentRequired at : %s",
                authorize_url.c_str() );
        return Status::WaitForAuthorizationCode;
    }

    void done( const cloudstorage::ICloudProvider&,
               cloudstorage::EitherError<void> error ) override
    {
        // An error occurred
        if ( error.left() )
        {
            var_SetString( p_access->obj.libvlc, "cloudstorage-auth", "ABORT" );
            msg_Err( p_access, "Authorization Error %d: %s",
                    error.left()->code_, error.left()->description_.c_str() );
            p_sys->authenticated = false;
        }
        else // No error occurred
        {
            p_sys->authenticated = true;
        }
    }

private:
    stream_t *p_access;
    access_sys_t *p_sys;
};

#endif
