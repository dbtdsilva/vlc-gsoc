/*****************************************************************************
 * cloudstorage.cpp: cloud access module using libcloudstorage for VLC
 *****************************************************************************
 * Copyright (C) 2017 William Ung
 *
 * Authors: William Ung <williamung@msn.com>
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

#include <sstream>
#include <algorithm>

#include <ICloudProvider.h>
#include <ICloudStorage.h>
#include <IItem.h>
#include <IRequest.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_access.h>
#include <vlc_services_discovery.h>
#include <vlc_keystore.h>

using cloudstorage::ICloudStorage;
using cloudstorage::ICloudProvider;
using cloudstorage::IDownloadFileCallback;
using cloudstorage::IItem;

static int Open(vlc_object_t *);
static void Close(vlc_object_t *);
static int SDOpen( vlc_object_t * );
static void SDClose( vlc_object_t * );
static int vlc_sd_probe_Open( vlc_object_t * );

static int readDir(stream_t *, input_item_node_t *);
static int add_item(struct access_fsdir *, stream_t *, IItem::Pointer);
static int getDir(stream_t *, input_item_node_t *);
static std::vector<std::string> parseUrl(std::string);

vlc_module_begin()
    set_shortname(N_("cloudstorage"))
    set_capability("access", 0)
    set_description(N_("cloudstorage input"))
    set_category(CAT_INPUT)
    set_subcategory(SUBCAT_INPUT_ACCESS)
    set_callbacks(Open, Close)

    add_submodule()
        set_description( N_("cloudstorage services discovery") )
        set_category( CAT_PLAYLIST )
        set_subcategory( SUBCAT_PLAYLIST_SD )
        set_capability( "services_discovery", 0 )
        set_callbacks( SDOpen, SDClose )

        VLC_SD_PROBE_SUBMODULE
vlc_module_end()

/*
**  private struct
*/

struct access_sys_t
{
    ICloudProvider::Pointer provider_;
    std::string provider_name_;
    std::string token_;

    vlc_keystore *p_keystore_;
    char *ppsz_values[KEY_MAX];

    IItem::Pointer current_item_;
    std::vector<std::string> directory_stack_;
    std::vector<IItem::Pointer> directory_list_;

    ICloudProvider::ListDirectoryRequest::Pointer list_directory_request_;

    access_sys_t(vlc_object_t *);
};

/*
**  Provider Callback
**  override to implement the behaviour we are looking for
*/

class Callback : public ICloudProvider::ICallback {
public:
    Callback(access_t *access, access_sys_t *sys) : p_access(access), p_sys(sys) {}

    Status userConsentRequired(const ICloudProvider& provider) override
    {
        msg_Info(p_access, "User ConsentRequired at : %s", provider.authorizeLibraryUrl().c_str());
        return Status::WaitForAuthorizationCode;
    }

    void accepted(const ICloudProvider& provider) override
    {
        p_sys->token_ = provider.token();
        vlc_keystore_store(p_sys->p_keystore_, p_sys->ppsz_values,
            (const uint8_t *)p_sys->token_.c_str(), p_sys->token_.size(),
            p_sys->provider_name_.c_str());
    }

    void declined(const ICloudProvider&) override
    {
        Close((vlc_object_t*)p_access);
    }

    void error(const ICloudProvider&, const std::string& desc) override
    {
        msg_Err(p_access, "%s", desc.c_str());
        Close((vlc_object_t*)p_access);
    }

    access_t *p_access;
    access_sys_t *p_sys;
};

access_sys_t::access_sys_t(vlc_object_t *p_this)
{
    vlc_keystore_entry *p_entries;

    provider_name_ = "box";
    p_keystore_ = vlc_get_memory_keystore(p_this);
    if (p_keystore_ == nullptr)
        throw std::bad_alloc();
    provider_ = cloudstorage::ICloudStorage::create()->provider(provider_name_);
    if (!provider_)
        throw std::bad_alloc();
    VLC_KEYSTORE_VALUES_INIT(ppsz_values);
    ppsz_values[KEY_PROTOCOL] = "cloudstorage";
    ppsz_values[KEY_USER] = "cloudstorage user";
    ppsz_values[KEY_SERVER] = "cloudstorage";

    if (vlc_keystore_find(p_keystore_, ppsz_values, &p_entries) > 0)
        token_ = (char *)p_entries[0].p_secret;
}

static int SDOpen( vlc_object_t *p_this ) {
    services_discovery_t *p_sd = (services_discovery_t *) p_this;
    p_sd->description = _("Services Discovery");

    msg_Dbg(p_sd, "Opened Services Discovery");
    cloudstorage::ICloudStorage::Pointer storage = cloudstorage::ICloudStorage::create();
    for ( const auto& provider : storage->providers() ) {
        const char * provider_name = provider->name().c_str();
        msg_Dbg( p_sd, "Provider name: %s", provider_name );

        char *uri;
        if (asprintf(&uri, "cloudstorage://%s/", provider_name) < 0)
            continue;
        input_item_t* p_item = input_item_New( uri, provider_name );
        if (p_item != NULL) {
            services_discovery_AddItem( p_sd, p_item );
            msg_Dbg( p_sd, "Added provider %s with URI: %s", provider_name, uri );
            input_item_Release ( p_item );
        }
        free(uri);
    }

    msg_Dbg( p_sd, "SDOpen ended" );
    return VLC_SUCCESS;
}

static void SDClose( vlc_object_t *p_this ) {
    msg_Dbg(p_this, "Closed Services Discovery");
}

static int vlc_sd_probe_Open( vlc_object_t *obj )
{
    fprintf(stderr, "Opened probe");
    vlc_probe_t *probe = (vlc_probe_t *) obj;

    return vlc_sd_probe_Add( probe, "cloudstorage", N_("Cloud Services"),
        SD_CAT_INTERNET );
}

static int add_item(struct access_fsdir *p_fsdir, stream_t *p_access, IItem::Pointer item)
{
    access_sys_t *p_sys = (access_sys_t*)p_access->p_sys;
    ICloudProvider::Pointer provider = p_sys->provider_;
    std::string url;
    int i_type;

    if (item->type() == IItem::FileType::Directory)
    {
        url = p_access->psz_url + item->filename() + "/";
        i_type = ITEM_TYPE_DIRECTORY;
    }
    else
    {
        item = provider->getItemDataAsync(item->id())->result();
        url = item->url();
        i_type = ITEM_TYPE_FILE;
    }
    return access_fsdir_additem(p_fsdir, url.c_str(), item->filename().c_str(), i_type, ITEM_NET);
}

static int readDir(stream_t *p_access, input_item_node_t *p_node)
{
    access_sys_t *p_sys = (access_sys_t*)p_access->p_sys;
    struct access_fsdir fsdir;

    p_sys->list_directory_request_ = p_sys->provider_->listDirectoryAsync(p_sys->current_item_);
    p_sys->directory_list_ = p_sys->list_directory_request_->result();
    access_fsdir_init(&fsdir, p_access, p_node);

    auto finish = [&](int error)
    {
        access_fsdir_finish(&fsdir, error);
        return error;
    };

    for (auto &i : p_sys->directory_list_)
        if (add_item(&fsdir, p_access, i))
            return finish(VLC_EGENERIC);
    return finish(VLC_SUCCESS);
}

static std::vector<std::string> parseUrl(std::string url)
{
    std::vector<std::string> parts;
    url = url.substr(15);     // length of "cloudstorage://"
    std::istringstream iss(url);

    for (std::string s; std::getline(iss, s, '/'); )
        parts.push_back(s);
    return parts;
}

static int getDir(stream_t *p_access, input_item_node_t *p_node)
{
    access_sys_t *p_sys = (access_sys_t*)p_access->p_sys;

    if (strcmp(p_access->psz_url, "cloudstorage://") == 0)
        readDir(p_access, p_node);
    else
    {
        p_sys->directory_stack_ = parseUrl(p_access->psz_url);
        for (auto &name : p_sys->directory_stack_)
        {
            p_sys->list_directory_request_ = p_sys->provider_->listDirectoryAsync(p_sys->current_item_);
            auto v = p_sys->list_directory_request_->result();
            p_sys->current_item_ = *std::find_if(v.begin(),v.end(),
                                                 [&name](IItem::Pointer item)
                                                 {return item->filename() == name;});
        }
        readDir(p_access, p_node);
    }
    return VLC_SUCCESS;
}

static int Open(vlc_object_t *p_this)
{
    access_t *p_access = (access_t*)p_this;
    access_sys_t *p_sys;

    try
    {
        p_access->p_sys = p_sys = new(std::nothrow) access_sys_t(p_this);
        if (p_sys == nullptr)
            return VLC_ENOMEM;
        p_sys->provider_->initialize(
            {
                p_sys->token_,
                std::unique_ptr<Callback>(new Callback((access_t*)p_this, p_sys)),
                nullptr, nullptr, nullptr, {}
            });
        p_sys->current_item_ = p_sys->provider_->rootDirectory();
        p_access->pf_control = access_vaDirectoryControlHelper;
        p_access->pf_readdir = getDir;
        return VLC_SUCCESS;
    }

    catch (std::exception& e)
    {
        msg_Err(p_access, "%s", e.what());
        goto error;
    }

 error:
    Close(p_this);
    return VLC_EGENERIC;
}

static void Close(vlc_object_t *p_this)
{
    access_t *p_access = (access_t*)p_this;
    access_sys_t *p_sys = (access_sys_t*)p_access->p_sys;

    delete p_sys;
}
