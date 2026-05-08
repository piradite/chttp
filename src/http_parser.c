#include "http_parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "../include/security.h"
#include "logger.h"

#define MAX_HEADERS 100
#define MAX_PATH_LEN 1024

typedef struct {
    char key[128];
    char value[256];
} HttpHeader;

struct HttpRequest {
    HttpMethod method;
    char path[MAX_PATH_LEN];
    HttpHeader headers[MAX_HEADERS];
    size_t header_count;
    bool connection_close;
    char* body;
    size_t body_len;
};

CHttpError http_request_create(HttpRequest** out_req) {
    if (!out_req)
        return CHTTP_ERR_INVAL;
    *out_req = (HttpRequest*)calloc(1, sizeof(HttpRequest));
    if (!*out_req)
        return CHTTP_ERR_OOM;
    return CHTTP_OK;
}

void http_request_destroy(HttpRequest* req) {
    if (req) {
        if (req->body) {
            free(req->body);
        }
        free(req);
    }
}

static void safe_strncpy(char* dest, const char* src, size_t n) {
    if (n == 0)
        return;
    size_t copy_len = n - 1;
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';
}

static bool url_decode(char* path) {
    char* src = path;
    char* dst = path;
    while (*src) {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = {src[1], src[2], '\0'};
            char decoded = (char)strtol(hex, NULL, 16);
            if (decoded == '\0')
                return false;
            *dst++ = decoded;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    return true;
}

CHttpError http_request_parse(HttpRequest* req, const char* buffer, size_t len,
                              size_t* bytes_parsed) {
    if (!req || !buffer || !bytes_parsed)
        return CHTTP_ERR_INVAL;

    req->method = HTTP_METHOD_UNKNOWN;
    req->path[0] = '\0';
    req->header_count = 0;
    req->connection_close = false;
    if (req->body) {
        free(req->body);
        req->body = NULL;
    }
    req->body_len = 0;

    const char* header_end = memmem(buffer, len, "\r\n\r\n", 4);
    if (!header_end) {
        if (memmem(buffer, len, "\n\n", 2)) {
            return CHTTP_ERR_BAD_REQUEST;
        }
        return CHTTP_ERR_INCOMPLETE;
    }

    *bytes_parsed = (header_end - buffer) + 4;
    const char* p = buffer;

    if (len >= 4 && strncmp(p, "GET ", 4) == 0) {
        req->method = HTTP_METHOD_GET;
        p += 4;
    } else if (len >= 5 && strncmp(p, "HEAD ", 5) == 0) {
        req->method = HTTP_METHOD_HEAD;
        p += 5;
    } else if (len >= 5 && strncmp(p, "POST ", 5) == 0) {
        req->method = HTTP_METHOD_POST;
        p += 5;
    } else
        return CHTTP_ERR_METHOD_NOT_ALLOWED;

    const char* path_end = memchr(p, ' ', header_end - p);
    if (!path_end)
        return CHTTP_ERR_BAD_REQUEST;

    size_t path_len = path_end - p;
    if (path_len >= MAX_PATH_LEN)
        return CHTTP_ERR_TOOLARGE;

    safe_strncpy(req->path, p, path_len + 1);

    char* query_ptr = strchr(req->path, '?');
    if (query_ptr) {
        *query_ptr = '\0';
    }

    if (!url_decode(req->path)) {
        return CHTTP_ERR_BAD_REQUEST;
    }

    p = path_end + 1;

    const char* line_end = memmem(p, (header_end + 2) - p, "\r\n", 2);
    if (!line_end)
        return CHTTP_ERR_BAD_REQUEST;

    bool is_http_1_1 = false;
    if ((size_t)(line_end - p) >= 8) {
        if (strncmp(p, "HTTP/1.0", 8) == 0) {
            is_http_1_1 = false;
            req->connection_close = true;
        } else if (strncmp(p, "HTTP/1.1", 8) == 0) {
            is_http_1_1 = true;
            req->connection_close = false;
        } else {
            return CHTTP_ERR_BAD_REQUEST;
        }
    } else {
        return CHTTP_ERR_BAD_REQUEST;
    }

    p = line_end + 2;

    while (p < header_end) {
        if (req->header_count >= MAX_HEADERS) {
            return CHTTP_ERR_BAD_REQUEST;
        }

        line_end = memmem(p, (header_end + 2) - p, "\r\n", 2);
        if (!line_end || line_end > header_end)
            break;
        if (line_end == p)
            break;

        const char* colon = memchr(p, ':', line_end - p);
        if (!colon)
            return CHTTP_ERR_BAD_REQUEST;

        size_t key_len = colon - p;
        if (key_len == 0 || key_len >= sizeof(req->headers[0].key))
            return CHTTP_ERR_BAD_REQUEST;

        safe_strncpy(req->headers[req->header_count].key, p, key_len + 1);

        if (strcasecmp(req->headers[req->header_count].key, "Content-Length") == 0) {
            if (http_request_get_header(req, "Content-Length") != NULL) {
                return CHTTP_ERR_BAD_REQUEST;
            }
        }

        const char* value_start = colon + 1;
        while (value_start < line_end && (*value_start == ' ' || *value_start == '\t')) {
            value_start++;
        }

        size_t value_len = line_end - value_start;
        if (value_len >= sizeof(req->headers[0].value))
            return CHTTP_ERR_BAD_REQUEST;

        safe_strncpy(req->headers[req->header_count].value, value_start, value_len + 1);

        if (strcasecmp(req->headers[req->header_count].key, "Connection") == 0) {
            if (strcasecmp(req->headers[req->header_count].value, "close") == 0) {
                req->connection_close = true;
            } else if (strcasecmp(req->headers[req->header_count].value, "keep-alive") == 0) {
                req->connection_close = false;
            }
        }

        req->header_count++;
        p = line_end + 2;
    }

    if (is_http_1_1 && !http_request_get_header(req, "Host")) {
        return CHTTP_ERR_BAD_REQUEST;
    }

    size_t header_size = (header_end - buffer) + 4;
    const char* cl_str = http_request_get_header(req, "Content-Length");
    if (cl_str) {
        char* endptr;
        long cl = strtol(cl_str, &endptr, 10);
        if (*endptr != '\0' || cl < 0) {
            return CHTTP_ERR_BAD_REQUEST;
        }
        if (cl > (long)(8 * 1024 * 1024)) {
            return CHTTP_ERR_TOOLARGE;
        }
        if (len < header_size + (size_t)cl) {
            return CHTTP_ERR_INCOMPLETE;
        }
        req->body_len = (size_t)cl;
        if (cl > 0) {
            req->body = (char*)calloc((size_t)cl + 1, 1);
            if (!req->body)
                return CHTTP_ERR_OOM;
            safe_memcpy(req->body, (size_t)cl, buffer + header_size, (size_t)cl);
            req->body[(size_t)cl] = '\0';
        }
        *bytes_parsed = header_size + (size_t)cl;
    } else {
        *bytes_parsed = header_size;
    }

    return CHTTP_OK;
}

HttpMethod http_request_get_method(const HttpRequest* req) {
    return req ? req->method : HTTP_METHOD_UNKNOWN;
}

const char* http_request_get_path(const HttpRequest* req) { return req ? req->path : NULL; }

const char* http_request_get_header(const HttpRequest* req, const char* key) {
    if (!req || !key)
        return NULL;
    for (size_t i = 0; i < req->header_count; ++i) {
        if (strcasecmp(req->headers[i].key, key) == 0) {
            return req->headers[i].value;
        }
    }
    return NULL;
}

bool http_request_should_close(const HttpRequest* req) {
    return req ? req->connection_close : true;
}

const char* http_request_get_body(const HttpRequest* req) { return req ? req->body : NULL; }

size_t http_request_get_body_len(const HttpRequest* req) { return req ? req->body_len : 0; }
