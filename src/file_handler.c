#define _GNU_SOURCE  // NOLINT(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp)
#include "file_handler.h"

#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "logger.h"

#define MAX_PATH 1024

static char g_root_dir[MAX_PATH];
static size_t g_root_dir_len;

static int safe_snprintf(char* str, size_t size, const char* format, ...) {
    va_list args;
    va_start(args, format);
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    int ret = vsnprintf(str, size, format, args);
    va_end(args);
    return ret;
}

CHttpError file_handler_init(const char* root_dir) {
    if (!root_dir)
        return CHTTP_ERR_INVAL;

    if (!realpath(root_dir, g_root_dir)) {
        LOG_ERROR("Failed to resolve root dir: %s", root_dir);
        return CHTTP_ERR_IO;
    }
    g_root_dir_len = strlen(g_root_dir);
    return CHTTP_OK;
}

CHttpError file_handler_sanitize_path(const char* input_path, char* out_path, size_t max_len) {
    if (!input_path || !out_path)
        return CHTTP_ERR_INVAL;

    if (strstr(input_path, "..") != NULL) {
        return CHTTP_ERR_FORBIDDEN;
    }

    const char* p = input_path;
    if (*p == '/')
        p++;

    if (strlen(p) == 0) {
        p = "index.html";
    }

    char full_path[MAX_PATH];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    if (safe_snprintf(full_path, sizeof(full_path), "%s/%s", g_root_dir, p) < 0) {
        return CHTTP_ERR_TOOLARGE;
    }

    char resolved_path[PATH_MAX];
    if (realpath(full_path, resolved_path) == NULL) {
        (void)safe_snprintf(out_path, max_len, "%s", full_path);
        return CHTTP_OK;
    }

    if (strncmp(resolved_path, g_root_dir, g_root_dir_len) != 0) {
        return CHTTP_ERR_FORBIDDEN;
    }

    (void)safe_snprintf(out_path, max_len, "%s", resolved_path);
    return CHTTP_OK;
}

const char* file_handler_get_mime_type(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext)
        return "application/octet-stream";

    if (strcasecmp(ext, ".html") == 0)
        return "text/html";
    if (strcasecmp(ext, ".css") == 0)
        return "text/css";
    if (strcasecmp(ext, ".js") == 0)
        return "application/javascript";
    if (strcasecmp(ext, ".png") == 0)
        return "image/png";
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    if (strcasecmp(ext, ".gif") == 0)
        return "image/gif";
    if (strcasecmp(ext, ".webp") == 0)
        return "image/webp";
    if (strcasecmp(ext, ".svg") == 0)
        return "image/svg+xml";
    if (strcasecmp(ext, ".mp4") == 0)
        return "video/mp4";
    if (strcasecmp(ext, ".json") == 0)
        return "application/json";

    return "application/octet-stream";
}

CHttpError file_handler_process_request(const HttpRequest* req, HttpResponse* res) {
    if (!req || !res)
        return CHTTP_ERR_INVAL;

    HttpMethod method = http_request_get_method(req);
    if (method != HTTP_METHOD_GET && method != HTTP_METHOD_HEAD && method != HTTP_METHOD_POST) {
        http_response_set_status(res, 405, http_request_should_close(req));
        http_response_add_header(res, "Content-Length", "0");
        http_response_add_header(res, "Allow", "GET, HEAD, POST");
        return CHTTP_OK;
    }
    const char* path = http_request_get_path(req);
    if (strcmp(path, "/reload") == 0) {
        http_response_set_status(res, 200, false);
        http_response_add_header(res, "Content-Type", "text/event-stream");
        http_response_add_header(res, "Cache-Control", "no-cache");
        http_response_add_header(res, "Connection", "keep-alive");
        return CHTTP_OK;
    }

    char safe_path[MAX_PATH];
    CHttpError err = file_handler_sanitize_path(path, safe_path, sizeof(safe_path));
    if (err == CHTTP_ERR_FORBIDDEN) {
        http_response_set_status(res, 403, http_request_should_close(req));
        http_response_add_header(res, "Content-Length", "0");
        return CHTTP_OK;
    } else if (err != CHTTP_OK) {
        http_response_set_status(res, 400, http_request_should_close(req));
        http_response_add_header(res, "Content-Length", "0");
        return CHTTP_OK;
    }

    struct stat st;
    if (stat(safe_path, &st) < 0 || S_ISDIR(st.st_mode)) {
        http_response_set_status(res, 404, http_request_should_close(req));
        http_response_add_header(res, "Content-Length", "0");
        return CHTTP_OK;
    }

    int fd = open(safe_path, O_RDONLY);
    if (fd < 0) {
        http_response_set_status(res, 403, http_request_should_close(req));
        http_response_add_header(res, "Content-Length", "0");
        return CHTTP_OK;
    }

    char last_modified[64];
    struct tm tm_info;
    gmtime_r(&st.st_mtime, &tm_info);
    (void)strftime(last_modified, sizeof(last_modified), "%a, %d %b %Y %H:%M:%S GMT", &tm_info);

    const char* if_mod_since = http_request_get_header(req, "If-Modified-Since");
    if (if_mod_since) {
        struct tm ims_tm = {0};
        if (strptime(if_mod_since, "%a, %d %b %Y %H:%M:%S GMT", &ims_tm)) {
            time_t ims_time = timegm(&ims_tm);
            if (ims_time >= st.st_mtime) {
                http_response_set_status(res, 304, http_request_should_close(req));
                http_response_add_header(res, "Content-Length", "0");
                close(fd);
                return CHTTP_OK;
            }
        }
    }

    http_response_set_status(res, 200, http_request_should_close(req));
    char* reload_script =
        "<script>const evt = new EventSource('/reload'); evt.onmessage = () => "
        "location.reload();</script>";
    const char* mime_type = file_handler_get_mime_type(safe_path);
    bool is_html = strstr(mime_type, "text/html") != NULL;

    char len_str[32];
    size_t total_len = (size_t)st.st_size;
    if (is_html) {
        total_len += strlen(reload_script);
    }
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    (void)safe_snprintf(len_str, sizeof(len_str), "%zu", total_len);
    http_response_add_header(res, "Content-Length", len_str);
    http_response_add_header(res, "Content-Type", mime_type);

    if (method == HTTP_METHOD_GET || method == HTTP_METHOD_POST) {
        FileBody body = {.fd = fd, .size = (size_t)st.st_size};
        http_response_set_file_body(res, body);
    } else {
        close(fd);
    }

    return CHTTP_OK;
}
