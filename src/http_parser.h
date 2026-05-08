#ifndef CHTTP_HTTP_PARSER_H
#define CHTTP_HTTP_PARSER_H

#include <stdbool.h>
#include <stddef.h>

#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HTTP_METHOD_GET,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_POST,
    HTTP_METHOD_UNKNOWN
} HttpMethod;

typedef struct HttpRequest HttpRequest;
CHttpError http_request_create(HttpRequest** out_req);
void http_request_destroy(HttpRequest* req);
CHttpError http_request_parse(HttpRequest* req, const char* buffer, size_t len,
                              size_t* bytes_parsed);

HttpMethod http_request_get_method(const HttpRequest* req);
const char* http_request_get_path(const HttpRequest* req);
const char* http_request_get_header(const HttpRequest* req, const char* key);
bool http_request_should_close(const HttpRequest* req);

const char* http_request_get_body(const HttpRequest* req);
size_t http_request_get_body_len(const HttpRequest* req);

#ifdef __cplusplus
}
#endif

#endif
