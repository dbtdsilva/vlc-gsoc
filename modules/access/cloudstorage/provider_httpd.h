#include <string>

#include "access.h"

using cloudstorage::IHttpd;

class Httpd : public IHttpd {
public:
    Httpd( access_t *access ) :
        p_access( access ), p_sys( (access_sys_t*) access->p_sys )
    {}

    void startServer(uint16_t port, CallbackFunction request_callback,
            void* data) override
    {

    }

    void stopServer() override
    {

    }

    std::string getArgument(RequestData*,
            const std::string& arg_name) override
    {

    }

    std::string getHeader(RequestData*,
            const std::string& header_name) override
    {

    }

    int sendResponse(RequestData*, const std::string& response) override
    {

    }

private:
    access_t *p_access;
    access_sys_t *p_sys;
};