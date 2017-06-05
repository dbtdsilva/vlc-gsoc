#include <string>

#include "access.h"

using cloudstorage::ICloudProvider;

class Callback : public ICloudProvider::ICallback {
public:
    Callback( access_t *access, access_sys_t *sys ) :
        p_access(access), p_sys(sys) {}

    Status userConsentRequired( const ICloudProvider& provider ) override
    {
        msg_Info( p_access, "User ConsentRequired at : %s",
                provider.authorizeLibraryUrl().c_str() );
        return Status::WaitForAuthorizationCode;
    }

    void accepted( const ICloudProvider& provider ) override
    {
        p_sys->token_ = provider.token();
        vlc_keystore_store( p_sys->p_keystore_, p_sys->ppsz_values,
            (const uint8_t *)p_sys->token_.c_str(), p_sys->token_.size(),
            p_sys->provider_name_.c_str() );
    }

    void declined( const ICloudProvider& ) override
    {
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