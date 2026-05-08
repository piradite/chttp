#include "http_response.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../include/security.h"

#define MAX_RESPONSE_HEADERS 20

typedef struct {
    char key[64];
    char value[128];
} ResponseHeader;

struct HttpResponse {
    int status_code;
    ResponseHeader headers[MAX_RESPONSE_HEADERS];
    size_t header_count;
    int file_fd;
    size_t file_size;
};

CHttpError http_response_create(HttpResponse** out_res) {
    if (!out_res)
        return CHTTP_ERR_INVAL;
    *out_res = (HttpResponse*)calloc(1, sizeof(HttpResponse));
    if (!*out_res)
        return CHTTP_ERR_OOM;
    (*out_res)->file_fd = -1;
    return CHTTP_OK;
}

void http_response_destroy(HttpResponse* res) { free(res); }

static const char* get_status_text(int code) {
    switch (code) {
        case 200:
            return "OK";
        case 400:
            return "Bad Request";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 500:
            return "Internal Server Error";
        default:
            return "Unknown";
    }
}

void http_response_set_status(HttpResponse* res, int status_code, bool close_connection) {
    if (!res)
        return;
    res->status_code = status_code;
    if (close_connection) {
        http_response_add_header(res, "Connection", "close");
    } else {
        http_response_add_header(res, "Connection", "keep-alive");
    }
}

int http_response_get_status_code(const HttpResponse* res) { return res ? res->status_code : 0; }

const char* http_response_get_header(const HttpResponse* res, const char* key) {
    if (!res || !key)
        return NULL;
    for (size_t i = 0; i < res->header_count; ++i) {
        if (strcasecmp(res->headers[i].key, key) == 0) {
            return res->headers[i].value;
        }
    }
    return NULL;
}

CHttpError http_response_add_header(HttpResponse* res, const char* key, const char* value) {
    if (!res || !key || !value)
        return CHTTP_ERR_INVAL;
    if (res->header_count >= MAX_RESPONSE_HEADERS)
        return CHTTP_ERR_OOM;

    safe_snprintf(res->headers[res->header_count].key, sizeof(res->headers[0].key), "%s", key);
    safe_snprintf(res->headers[res->header_count].value, sizeof(res->headers[0].value), "%s",
                  value);
    res->header_count++;

    return CHTTP_OK;
}

CHttpError http_response_serialize_headers(const HttpResponse* res, char* buffer, size_t max_len,
                                           size_t* out_len) {
    if (!res || !buffer || !out_len)
        return CHTTP_ERR_INVAL;

    int written = safe_snprintf(buffer, max_len, "HTTP/1.1 %d %s\r\n", res->status_code,
                                get_status_text(res->status_code));
    if (written < 0 || (size_t)written >= max_len)
        return CHTTP_ERR_TOOLARGE;

    size_t offset = (size_t)written;

    int has_date = 0;
    for (size_t i = 0; i < res->header_count; ++i) {
        if (strcasecmp(res->headers[i].key, "Date") == 0) {
            has_date = 1;
        }
        written = safe_snprintf(buffer + offset, max_len - offset, "%s: %s\r\n",
                                res->headers[i].key, res->headers[i].value);
        if (written < 0 || (size_t)written >= (max_len - offset))
            return CHTTP_ERR_TOOLARGE;
        offset += (size_t)written;
    }

    if (!has_date) {
        time_t now = time(NULL);
        struct tm tm_info;
        gmtime_r(&now, &tm_info);
        char date_buf[64];
        (void)strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %H:%M:%S GMT", &tm_info);
        written = safe_snprintf(buffer + offset, max_len - offset, "Date: %s\r\n", date_buf);
        if (written < 0 || (size_t)written >= (max_len - offset))
            return CHTTP_ERR_TOOLARGE;
        offset += (size_t)written;
    }

    if (max_len - offset < 2)
        return CHTTP_ERR_TOOLARGE;

    buffer[offset++] = '\r';
    buffer[offset++] = '\n';
    buffer[offset] = '\0';

    *out_len = offset;
    return CHTTP_OK;
}

void http_response_set_file_body(HttpResponse* res, FileBody body) {
    if (!res)
        return;
    res->file_fd = body.fd;
    res->file_size = body.size;
}

int http_response_get_file_fd(const HttpResponse* res) { return res ? res->file_fd : -1; }

size_t http_response_get_file_size(const HttpResponse* res) { return res ? res->file_size : 0; }
