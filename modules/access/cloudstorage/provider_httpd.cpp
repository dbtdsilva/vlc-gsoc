#include "provider_httpd.h"

#include <sstream>
#include <map>

using cloudstorage::IHttpd;

int Httpd::internalCallback( httpd_callback_sys_t * args, httpd_client_t * client,
        httpd_message_t * answer, const httpd_message_t * query )
{
    fprintf(stderr, "cloudstorage: %s\n", (unsigned char*) query->psz_args);
    CallbackData* data = (CallbackData *) args;
    fprintf(stderr, "cloudstorage: %s\n", (unsigned char*) query->psz_args);

    RequestData request_data;
    fprintf(stderr, "cloudstorage: %s\n", (char*) query->psz_args);
    request_data.obj = data->obj;
    request_data.url.assign(query->psz_url);
    request_data.internal_data = answer;
    request_data.custom_data = data->custom_data;

    // Fill the structure with the arguments in the query
    std::string argument;
    std::istringstream iss( std::string( (char*) query->psz_args ) );
    while (std::getline(iss, argument, '&')) {
        fprintf(stderr, "cloudstorage: token: %s\n", argument.c_str());
        size_t equal_div = argument.find('=');
        if (equal_div == std::string::npos)
            continue;

        std::string key = argument.substr( 0, equal_div );
        std::string value = argument.substr( equal_div + 1 );
        request_data.args.insert( std::make_pair( key, value ) );
    }
    // Fill the headers
    /*for (unsigned int i = 0; i < query->i_headers; i++) {
        request_data.headers.insert( std::string( query->p_headers[i].name ),
                                     std::string( query->p_headers[i].value ) );
    }*/

    int error = data->request_callback(&request_data);
    return error < 0 ? VLC_EGENERIC : VLC_SUCCESS;
}

Httpd::Httpd( access_t *access ) :
        p_access( access ), p_sys( (access_sys_t*) access->p_sys )
{
}

void Httpd::startServer(uint16_t port, CallbackFunction request_callback,
        void* data)
{
    CallbackData* callback_data = new CallbackData();
    callback_data->obj = this;
    callback_data->request_callback = request_callback;
    callback_data->custom_data = data;

    var_SetInteger( p_access->obj.libvlc, "http-port", 12345 );

    host = vlc_http_HostNew( VLC_OBJECT( p_access ) );
    url_root = httpd_UrlNew( host, "/", NULL, NULL);
    url_login = httpd_UrlNew( host, "/login", NULL, NULL);

    httpd_UrlCatch(url_root, HTTPD_MSG_HEAD, internalCallback,
            (httpd_callback_sys_t*) callback_data);
    httpd_UrlCatch(url_root, HTTPD_MSG_GET, internalCallback,
            (httpd_callback_sys_t*) callback_data);
    httpd_UrlCatch(url_root, HTTPD_MSG_POST, internalCallback,
            (httpd_callback_sys_t*) callback_data);

    httpd_UrlCatch(url_login, HTTPD_MSG_HEAD, internalCallback,
            (httpd_callback_sys_t*) callback_data);
    httpd_UrlCatch(url_login, HTTPD_MSG_GET, internalCallback,
            (httpd_callback_sys_t*) callback_data);
    httpd_UrlCatch(url_login, HTTPD_MSG_POST, internalCallback,
            (httpd_callback_sys_t*) callback_data);
}

void Httpd::stopServer()
{
    httpd_UrlDelete( url_root );
    httpd_UrlDelete( url_login );
    httpd_HostDelete ( host );
}

std::string Httpd::getArgument(RequestData* data,
        const std::string& arg_name)
{
    auto it = data->args.find(arg_name);
    return it == data->args.end() ? "" : it->second;
}

std::string Httpd::getHeader(RequestData* data,
        const std::string& header_name)
{
    auto it = data->headers.find(header_name);
    return it == data->headers.end() ? "" : it->second;
}

int Httpd::sendResponse(RequestData* data, const std::string& response)
{
    httpd_message_t * answer = (httpd_message_t *) data->internal_data;

    answer->i_proto = HTTPD_PROTO_HTTP;
    answer->i_version= 1;
    answer->i_type = HTTPD_MSG_ANSWER;
    answer->i_status = 200;
    answer->i_headers = 0;

    answer->i_body = response.length();
    char * message = strdup( response.c_str() );
    answer->p_body = (unsigned char *) message;

    httpd_MsgAdd(answer, "Content-Length", "%d", answer->i_body);

    return 0;
}