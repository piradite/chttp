#ifndef CHTTP_H
#define CHTTP_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ssize_t (*recv)(int fd, void* buf, size_t n, int flags);
    ssize_t (*send)(int fd, const void* buf, size_t n, int flags);
    ssize_t (*sendfile)(int out_fd, int in_fd, off_t* offset, size_t count);
} CHttpIoOps;

typedef struct HttpServer HttpServer;

typedef struct {
    uint16_t port;
    const char* root_dir;
    int max_connections;
    bool live_reload;
    CHttpIoOps* io_ops;
} HttpServerConfig;

int chttp_server_create(const HttpServerConfig* config, HttpServer** out_server);
int chttp_server_run(HttpServer* server);

void chttp_server_destroy(HttpServer* server);

#ifdef __cplusplus
}
#endif

#endif
