#include <string>

#include "access.h"
#include <vlc_httpd.h>

using cloudstorage::IHttpd;

class Httpd : public IHttpd {
public:
    Httpd( access_t *access );
    void startServer(uint16_t port, CallbackFunction request_callback,
            void* data) override;
    void stopServer() override;

    std::string getArgument(RequestData*,
            const std::string& arg_name) override;
    std::string getHeader(RequestData*,
            const std::string& header_name) override;
    int sendResponse(RequestData*, const std::string& response) override;
private:
    static int internalCallback( httpd_callback_sys_t * args, httpd_client_t *,
            httpd_message_t * answer, const httpd_message_t * query_t );
    access_t *p_access;
    access_sys_t *p_sys;
    httpd_host_t *host;
    httpd_url_t *url_root, *url_login;

    struct CallbackData {
        IHttpd* obj;
        CallbackFunction request_callback;
        void* custom_data;
    };
};