#ifndef CHTTP_HTTP_RESPONSE_H
#define CHTTP_HTTP_RESPONSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "error.h"
#include "http_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HttpResponse HttpResponse;
CHttpError http_response_create(HttpResponse** out_res);
void http_response_destroy(HttpResponse* res);
void http_response_set_status(HttpResponse* res, int status_code, bool close_connection);

CHttpError http_response_add_header(HttpResponse* res, const char* key, const char* value);
int http_response_get_status_code(const HttpResponse* res);
const char* http_response_get_header(const HttpResponse* res, const char* key);
CHttpError http_response_serialize_headers(const HttpResponse* res, char* buffer, size_t max_len,
                                           size_t* out_len);

typedef struct {
    int fd;
    size_t size;
} FileBody;

void http_response_set_file_body(HttpResponse* res, FileBody body);
int http_response_get_file_fd(const HttpResponse* res);
size_t http_response_get_file_size(const HttpResponse* res);

#ifdef __cplusplus
}
#endif

#endif
