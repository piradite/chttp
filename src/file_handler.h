#ifndef CHTTP_FILE_HANDLER_H
#define CHTTP_FILE_HANDLER_H

#include "error.h"
#include "http_parser.h"
#include "http_response.h"

#ifdef __cplusplus
extern "C" {
#endif

CHttpError file_handler_init(const char* root_dir);
CHttpError file_handler_sanitize_path(const char* input_path, char* out_path, size_t max_len);
CHttpError file_handler_process_request(const HttpRequest* req, HttpResponse* res);
const char* file_handler_get_mime_type(const char* path);

#ifdef __cplusplus
}
#endif

#endif
