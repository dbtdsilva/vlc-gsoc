/*****************************************************************************
 * provider_callback.h: Inherit class ICallback from libcloudstorage
 *****************************************************************************
 * Copyright (C) 2017
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

class Callback : public ICloudProvider::ICallback {
public:
    Callback( access_t *access ) :
        p_access( access ), p_sys( (access_sys_t*) access->p_sys )
    {}

    Status userConsentRequired( const ICloudProvider& provider ) override
    {
        msg_Info( p_access, "User ConsentRequired at : %s",
                provider.authorizeLibraryUrl().c_str() );
        return Status::WaitForAuthorizationCode;
    }

    void accepted( const ICloudProvider& provider ) override
    {
        if ( p_sys->p_keystore == nullptr )
            return;

        std::stringstream ss_psz_label;
        ss_psz_label << "vlc_" << p_sys->username << "@" << p_sys->provider_name;

        vlc_keystore_entry* pp_entries;
        unsigned int i_entries = vlc_keystore_find( p_sys->p_keystore,
                p_sys->ppsz_values, &pp_entries );

        p_sys->token = provider.token();
        vlc_keystore_store( p_sys->p_keystore, p_sys->ppsz_values,
            (const uint8_t *)p_sys->token.c_str(), p_sys->token.size(),
            ss_psz_label.str().c_str() );

        // TODO: Hints should be stored and recovered from the keystore as well
        //       However, they are a map of strings -> it should be serialized
        // vlc_keystore_store( .. p_sys->provider->hints() );

        if ( i_entries == 0 )
        {
            msg_Dbg( p_access, "%s (new) was authenticated at %s",
                     p_sys->username.c_str(), p_sys->provider_name.c_str() );
            std::stringstream ss_user;
            ss_user << p_sys->username << "@" << p_sys->provider_name;
            var_SetString( p_access->obj.libvlc, "cloud-new-auth",
                    ss_user.str().c_str() );
        }
        msg_Dbg( p_access, "Accepted credentials!");
    }

    void declined( const ICloudProvider& ) override
    {
        msg_Err( p_access, "Access was declined" );
        Close( (vlc_object_t*)p_access );
    }

    void error( const ICloudProvider&, const std::string& desc ) override
    {
        msg_Err( p_access, "%s", desc.c_str() );
        Close( (vlc_object_t*)p_access );
    }

    access_t *p_access;
    access_sys_t *p_sys;
};

#endif