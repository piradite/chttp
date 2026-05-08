#define _GNU_SOURCE  // NOLINT(bugprone-reserved-identifier, cert-dcl37-c, cert-dcl51-cpp)
#include "server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <unistd.h>

#include "connection.h"
#include "file_handler.h"
#include "logger.h"

#define MAX_EVENTS 1024

struct HttpServer {
    int listen_fd;
    int epoll_fd;
    int inotify_fd;
    int inotify_wd;
    bool running;
    HttpServerConfig config;

    Connection** connections;
    int connection_count;
};

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int chttp_server_create(const HttpServerConfig* config, HttpServer** out_server) {
    if (!config || !out_server)
        return CHTTP_ERR_INVAL;

    HttpServer* server = (HttpServer*)calloc(1, sizeof(HttpServer));
    if (!server)
        return CHTTP_ERR_OOM;

    server->config = *config;

    CHttpError err = file_handler_init(config->root_dir);
    if (err != CHTTP_OK) {
        free(server);
        return err;
    }

    server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->listen_fd < 0) {
        LOG_ERROR("Failed to create socket: %s", strerror(errno));
        free(server);
        return CHTTP_ERR_SYS;
    }

    int opt = 1;
    setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (set_nonblocking(server->listen_fd) < 0) {
        LOG_ERROR("Failed to set non-blocking on listen socket");
        close(server->listen_fd);
        free(server);
        return CHTTP_ERR_SYS;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(config->port);

    if (bind(server->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("Failed to bind to port %d: %s", config->port, strerror(errno));
        close(server->listen_fd);
        free(server);
        return CHTTP_ERR_SYS;
    }

    if (listen(server->listen_fd, SOMAXCONN) < 0) {
        LOG_ERROR("Failed to listen: %s", strerror(errno));
        close(server->listen_fd);
        free(server);
        return CHTTP_ERR_SYS;
    }

    server->epoll_fd = epoll_create1(0);
    if (server->epoll_fd < 0) {
        LOG_ERROR("Failed to create epoll: %s", strerror(errno));
        close(server->listen_fd);
        free(server);
        return CHTTP_ERR_SYS;
    }

    server->inotify_fd = -1;
    if (config->live_reload) {
        server->inotify_fd = inotify_init1(IN_NONBLOCK);
        if (server->inotify_fd >= 0) {
            server->inotify_wd = inotify_add_watch(server->inotify_fd, config->root_dir,
                                                   IN_MODIFY | IN_CREATE | IN_DELETE);
            struct epoll_event ev;
            ev.events = EPOLLIN | EPOLLET;
            ev.data.fd = server->inotify_fd;
            epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, server->inotify_fd, &ev);
            LOG_INFO("Live-reload enabled watching: %s", config->root_dir);
        } else {
            LOG_WARN("Could not initialize inotify: %s", strerror(errno));
        }
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.ptr = server;

    if (epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, server->listen_fd, &ev) < 0) {
        LOG_ERROR("Failed to add listen fd to epoll: %s", strerror(errno));
        close(server->epoll_fd);
        close(server->listen_fd);
        free(server);
        return CHTTP_ERR_SYS;
    }

    server->running = false;
    server->config = *config;
    server->connections = (Connection**)calloc(config->max_connections, sizeof(Connection*));
    server->connection_count = 0;
    *out_server = server;

    LOG_INFO("Server created and listening on port %d", config->port);
    return CHTTP_OK;
}

static void handle_new_connections(HttpServer* server) {
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd =
            accept4(server->listen_fd, (struct sockaddr*)&client_addr, &client_len, SOCK_NONBLOCK);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            LOG_WARN("Failed to accept connection: %s", strerror(errno));
            break;
        }

        Connection* conn = NULL;
        if (connection_create(client_fd, server->config.io_ops, &conn) != CHTTP_OK) {
            LOG_ERROR("Failed to create connection object");
            close(client_fd);
            continue;
        }

        for (int j = 0; j < server->config.max_connections; ++j) {
            if (server->connections[j] == NULL) {
                server->connections[j] = conn;
                break;
            }
        }

        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
        ev.data.ptr = conn;

        if (epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
            LOG_ERROR("Failed to add client fd to epoll: %s", strerror(errno));
            connection_destroy(conn);
        }
    }
}

int chttp_server_run(HttpServer* server) {
    if (!server)
        return CHTTP_ERR_INVAL;

    server->running = true;
    struct epoll_event events[MAX_EVENTS];

    LOG_INFO("Server started event loop");

    while (server->running) {
        int n = epoll_wait(server->epoll_fd, events, MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOG_ERROR("epoll_wait error: %s", strerror(errno));
            return CHTTP_ERR_SYS;
        }

        for (int i = 0; i < n; ++i) {
            if (events[i].data.ptr == server) {
                handle_new_connections(server);
            } else if (events[i].data.fd == server->inotify_fd) {
                char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
                const struct inotify_event* event;
                ssize_t len;
                char* ptr;

                while ((len = read(server->inotify_fd, buf, sizeof(buf))) > 0) {
                    for (ptr = buf; ptr < buf + len;
                         ptr += sizeof(struct inotify_event) + event->len) {
                        event = (const struct inotify_event*)ptr;
                        if (event->len > 0) {
                            LOG_INFO("Change detected in %s, reloading...", event->name);
                        } else {
                            LOG_INFO("reloading");
                        }
                    }
                }

                for (int j = 0; j < server->config.max_connections; ++j) {
                    if (server->connections[j] && connection_is_sse(server->connections[j])) {
                        connection_send_reload(server->connections[j]);
                    }
                }
            } else {
                Connection* conn = (Connection*)events[i].data.ptr;

                bool is_readable = (events[i].events & EPOLLIN) || (events[i].events & EPOLLRDHUP);
                bool is_writable = (events[i].events & EPOLLOUT);

                CHttpError err = connection_process(conn, is_readable, is_writable);
                if (err != CHTTP_OK || connection_is_closing(conn) ||
                    (events[i].events & (EPOLLERR | EPOLLHUP))) {
                    epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, connection_get_fd(conn), NULL);

                    for (int j = 0; j < server->config.max_connections; ++j) {
                        if (server->connections[j] == conn) {
                            server->connections[j] = NULL;
                            break;
                        }
                    }

                    connection_destroy(conn);
                }
            }
        }
    }

    LOG_INFO("Server event loop stopped");
    return CHTTP_OK;
}

void chttp_server_destroy(HttpServer* server) {
    if (!server)
        return;

    server->running = false;

    if (server->epoll_fd >= 0) {
        close(server->epoll_fd);
    }
    if (server->listen_fd >= 0) {
        close(server->listen_fd);
    }

    free(server);
    LOG_INFO("Server destroyed");
}
