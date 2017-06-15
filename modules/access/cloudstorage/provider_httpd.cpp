#include "provider_httpd.h"
#include "vlc_plugin.h"

#include <sstream>
#include <map>

using cloudstorage::IHttpd;

int Httpd::internalCallback( httpd_callback_sys_t * args, httpd_client_t * client,
        httpd_message_t * answer, const httpd_message_t * query )
{
    (void) client;

    CallbackData* data = (CallbackData *) args;

    RequestData request_data;
    request_data.url.assign(query->psz_url);
    request_data.custom_data = data->custom_data;

    // Fill the structure with the arguments in the query
    std::string argument;
    if ( query->psz_args != nullptr )
    {
        std::istringstream iss( std::string( (char*) query->psz_args ) );
        while (std::getline(iss, argument, '&')) {
            size_t equal_div = argument.find('=');
            if (equal_div == std::string::npos)
                continue;

            std::string key = argument.substr( 0, equal_div );
            std::string value = argument.substr( equal_div + 1 );
            request_data.args.insert( std::make_pair( key, value ) );
        }
    }
    // Fill the headers
    for (unsigned int i = 0; i < query->i_headers; i++) {
        request_data.headers.insert( std::make_pair(
            query->p_headers[i].name, query->p_headers[i].value ) );
    }

    std::string response = data->request_callback(&request_data);

    answer->i_proto = HTTPD_PROTO_HTTP;
    answer->i_version= 1;
    answer->i_type = HTTPD_MSG_ANSWER;
    answer->i_status = 200;
    answer->i_headers = 0;

    answer->i_body = response.length();
    char * message = strdup( response.c_str() );
    answer->p_body = (unsigned char *) message;

    httpd_MsgAdd(answer, "Content-Length", "%d", answer->i_body);

    return VLC_SUCCESS;
}

Httpd::Httpd( access_t *access ) :
        p_access( access ), p_sys( (access_sys_t*) access->p_sys )
{
}

void Httpd::startServer(uint16_t port, CallbackFunction request_callback,
        void* data)
{
    CallbackData* callback_data = new CallbackData();
    callback_data->request_callback = request_callback;
    callback_data->custom_data = data;

    // Initialize cloud httpd server with the requested port and restore the
    // default one at the end, other internal services from libcloudstorage
    // might request a second server (like MegaNz).
    int default_port = var_GetInteger( p_access->obj.libvlc, "http-port" );
    var_SetInteger( p_access->obj.libvlc, "http-port", port );
    host = vlc_http_HostNew( VLC_OBJECT( p_access ) );
    var_SetInteger( p_access->obj.libvlc, "http-port", default_port );

    // Register the possible URLs
    url_root = httpd_UrlNew( host, "/auth", NULL, NULL );
    url_login = httpd_UrlNew( host, "/auth/login", NULL, NULL );

    httpd_UrlCatch( url_root, HTTPD_MSG_HEAD, internalCallback,
            (httpd_callback_sys_t*) callback_data );
    httpd_UrlCatch( url_root, HTTPD_MSG_GET, internalCallback,
            (httpd_callback_sys_t*) callback_data );
    httpd_UrlCatch( url_root, HTTPD_MSG_POST, internalCallback,
            (httpd_callback_sys_t*) callback_data );

    httpd_UrlCatch( url_login, HTTPD_MSG_HEAD, internalCallback,
            (httpd_callback_sys_t*) callback_data );
    httpd_UrlCatch( url_login, HTTPD_MSG_GET, internalCallback,
            (httpd_callback_sys_t*) callback_data );
    httpd_UrlCatch( url_login, HTTPD_MSG_POST, internalCallback,
            (httpd_callback_sys_t*) callback_data );
}

void Httpd::stopServer()
{
    if ( host != nullptr )
    {
        if ( url_root != nullptr )
            httpd_UrlDelete( url_root );
        if ( url_login != nullptr )
            httpd_UrlDelete( url_login );
        httpd_HostDelete ( host );
    }
}