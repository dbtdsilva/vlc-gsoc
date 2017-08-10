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

#include <string>

#include "access.h"

using cloudstorage::ICloudProvider;
using cloudstorage::EitherError;

class Callback : public ICloudProvider::IAuthCallback {
public:
    Callback( stream_t *access ) :
        p_access( access ), p_sys( (access_sys_t *) access->p_sys )
    {}

    Status userConsentRequired( const ICloudProvider& provider ) override
    {
        msg_Info( p_access, "User ConsentRequired at : %s",
                provider.authorizeLibraryUrl().c_str() );
        return Status::WaitForAuthorizationCode;
    }

    void done( const ICloudProvider& provider,
               EitherError<void> error ) override
    {
        // An error occurred
        if ( error.left() )
        {
            msg_Err( p_access, "Authorization Error %d: %s",
                    error.left()->code_, error.left()->description_.c_str() );
        }
        else // No error occurred
        {
            accepted( provider );
        }
    }

    void accepted( const ICloudProvider& provider )
    {
        vlc_credential credentials;
        vlc_credential_init( &credentials, &p_sys->url );
        bool found = vlc_credential_get( &credentials, p_access,
                NULL, NULL, NULL, NULL );

        p_sys->token = provider.token();
        p_sys->hints = provider.hints();

        // Store hints and token
        std::string serialized_value = ICloudProvider::serializeSession(
                p_sys->token, p_sys->hints );

        // Store the data related with the session using the credentials API
        credentials.b_store = !p_sys->memory_keystore;
        credentials.psz_password = serialized_value.c_str();
        if ( !vlc_credential_store( &credentials, p_access ) )
        {
            msg_Warn( p_access, "Failed to store the credentials");
        }

        // Inform about new authentications
        if ( !found )
        {
            msg_Dbg( p_access, "%s (new) was authenticated at %s",
                     p_sys->url.psz_username, p_sys->url.psz_host );
            std::stringstream ss_user;
            ss_user << p_sys->url.psz_username << "@" << p_sys->url.psz_host;
            var_SetString( p_access->obj.libvlc, "cloudstorage-new-auth",
                    ss_user.str().c_str() );
        }
        vlc_credential_clean( &credentials );
        msg_Dbg( p_access, "Accepted credentials!");
    }

private:
    stream_t *p_access;
    access_sys_t *p_sys;
};

#endif
