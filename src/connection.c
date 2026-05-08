#include "connection.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../include/security.h"
#include "file_handler.h"
#include "logger.h"

#define READ_BUFFER_SIZE 8192
#define WRITE_BUFFER_SIZE 8192

struct Connection {
    int fd;
    ConnectionState state;
    CHttpIoOps io_ops;

    char read_buffer[READ_BUFFER_SIZE];
    size_t read_offset;

    HttpRequest* req;
    HttpResponse* res;
    size_t req_bytes_parsed;

    char write_buffer[WRITE_BUFFER_SIZE];
    size_t write_length;
    size_t write_offset;

    off_t file_offset;
    bool is_sse_connection;
};

CHttpError connection_create(int fd, const CHttpIoOps* io_ops, Connection** out_conn) {
    if (!out_conn)
        return CHTTP_ERR_INVAL;

    Connection* conn = (Connection*)calloc(1, sizeof(Connection));
    if (!conn)
        return CHTTP_ERR_OOM;

    conn->fd = fd;
    conn->state = CONN_STATE_READING;

    if (io_ops) {
        conn->io_ops = *io_ops;
    }
    if (!conn->io_ops.recv)
        conn->io_ops.recv = recv;
    if (!conn->io_ops.send)
        conn->io_ops.send = send;
    if (!conn->io_ops.sendfile)
        conn->io_ops.sendfile = sendfile;

    CHttpError err = http_request_create(&conn->req);
    if (err != CHTTP_OK) {
        free(conn);
        return err;
    }

    err = http_response_create(&conn->res);
    if (err != CHTTP_OK) {
        http_request_destroy(conn->req);
        free(conn);
        return err;
    }

    *out_conn = conn;
    return CHTTP_OK;
}

void connection_destroy(Connection* conn) {
    if (!conn)
        return;

    if (conn->fd >= 0) {
        close(conn->fd);
    }

    if (conn->res && http_response_get_file_fd(conn->res) >= 0) {
        close(http_response_get_file_fd(conn->res));
    }

    http_request_destroy(conn->req);
    http_response_destroy(conn->res);
    free(conn);
}

static void connection_reset_for_keep_alive(Connection* conn) {
    if (conn->res && http_response_get_file_fd(conn->res) >= 0) {
        close(http_response_get_file_fd(conn->res));
    }

    http_request_destroy(conn->req);
    http_response_destroy(conn->res);

    conn->req = NULL;
    conn->res = NULL;

    http_request_create(&conn->req);
    http_response_create(&conn->res);

    if (conn->read_offset > conn->req_bytes_parsed) {
        size_t remaining = conn->read_offset - conn->req_bytes_parsed;
        safe_memcpy(conn->read_buffer, READ_BUFFER_SIZE, conn->read_buffer + conn->req_bytes_parsed,
                    remaining);
        conn->read_offset = remaining;
    } else {
        conn->read_offset = 0;
    }

    conn->req_bytes_parsed = 0;
    conn->write_length = 0;
    conn->write_offset = 0;
    conn->file_offset = 0;
    conn->state = CONN_STATE_READING;
}

static CHttpError connection_do_read(Connection* conn) {
    while (1) {
        if (conn->read_offset >= READ_BUFFER_SIZE - 1) {
            return CHTTP_ERR_TOOLARGE;
        }

        ssize_t bytes = conn->io_ops.recv(conn->fd, conn->read_buffer + conn->read_offset,
                                          READ_BUFFER_SIZE - conn->read_offset - 1, 0);

        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return CHTTP_OK;
            }
            return CHTTP_ERR_IO;
        } else if (bytes == 0) {
            conn->state = CONN_STATE_CLOSING;
            return CHTTP_OK;
        }

        conn->read_offset += bytes;
        conn->read_buffer[conn->read_offset] = '\0';

        size_t parsed = 0;
        CHttpError parse_err =
            http_request_parse(conn->req, conn->read_buffer, conn->read_offset, &parsed);

        if (parse_err == CHTTP_OK) {
            conn->req_bytes_parsed = parsed;
            conn->state = CONN_STATE_HANDLING;
            return CHTTP_OK;
        } else if (parse_err != CHTTP_ERR_INCOMPLETE) {
            conn->req_bytes_parsed = conn->read_offset;
            conn->state = CONN_STATE_HANDLING;
            return CHTTP_OK;
        }
    }
}

static CHttpError connection_do_send_headers(Connection* conn) {
    while (conn->write_offset < conn->write_length) {
        ssize_t bytes = conn->io_ops.send(conn->fd, conn->write_buffer + conn->write_offset,
                                          conn->write_length - conn->write_offset, 0);

        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return CHTTP_OK;
            }
            return CHTTP_ERR_IO;
        }

        conn->write_offset += (size_t)bytes;
    }

    if (http_response_get_file_fd(conn->res) >= 0) {
        conn->state = CONN_STATE_SENDING_BODY;
    } else {
        if (http_request_should_close(conn->req)) {
            conn->state = CONN_STATE_CLOSING;
        } else {
            connection_reset_for_keep_alive(conn);
        }
    }
    return CHTTP_OK;
}

static CHttpError connection_do_send_body(Connection* conn) {
    int file_fd = http_response_get_file_fd(conn->res);
    size_t file_size = http_response_get_file_size(conn->res);

    while (conn->file_offset < (off_t)file_size) {
        ssize_t bytes = conn->io_ops.sendfile(conn->fd, file_fd, &conn->file_offset,
                                              file_size - conn->file_offset);

        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return CHTTP_OK;
            }
            return CHTTP_ERR_IO;
        }
    }

    const char* content_type = http_response_get_header(conn->res, "Content-Type");
    if (content_type && strstr(content_type, "text/html")) {
        char* script =
            "<script>const evt = new EventSource('/reload'); evt.onmessage = () => "
            "location.reload();</script>";
        send(conn->fd, script, strlen(script), 0);
    }

    if (http_request_should_close(conn->req)) {
        conn->state = CONN_STATE_CLOSING;
    } else {
        connection_reset_for_keep_alive(conn);
    }
    return CHTTP_OK;
}

CHttpError connection_process(Connection* conn, bool is_readable, bool is_writable) {
    if (!conn)
        return CHTTP_ERR_INVAL;

    bool progress = true;
    while (progress && conn->state != CONN_STATE_CLOSING) {
        ConnectionState old_state = conn->state;

        switch (conn->state) {
            case CONN_STATE_READING:
            case CONN_STATE_PARSING:
                if (is_readable) {
                    if (connection_do_read(conn) != CHTTP_OK) {
                        conn->state = CONN_STATE_CLOSING;
                    }
                }
                break;

            case CONN_STATE_HANDLING: {
                CHttpError err = file_handler_process_request(conn->req, conn->res);
                if (err != CHTTP_OK) {
                    http_response_set_status(conn->res, 500, true);
                    http_response_add_header(conn->res, "Content-Length", "0");
                }

                const char* content_type = http_response_get_header(conn->res, "Content-Type");
                if (content_type && strcmp(content_type, "text/event-stream") == 0) {
                    conn->is_sse_connection = true;
                }

                if (http_response_serialize_headers(conn->res, conn->write_buffer,
                                                    WRITE_BUFFER_SIZE,
                                                    &conn->write_length) != CHTTP_OK) {
                    conn->state = CONN_STATE_CLOSING;
                } else {
                    conn->write_offset = 0;
                    conn->state = CONN_STATE_SENDING_HEADERS;
                }
                break;
            }

            case CONN_STATE_SENDING_HEADERS:
                if (is_writable) {
                    if (connection_do_send_headers(conn) != CHTTP_OK) {
                        conn->state = CONN_STATE_CLOSING;
                    }
                }
                break;

            case CONN_STATE_SENDING_BODY:
                if (is_writable) {
                    if (connection_do_send_body(conn) != CHTTP_OK) {
                        conn->state = CONN_STATE_CLOSING;
                    }
                }
                break;

            case CONN_STATE_CLOSING:
                break;
        }

        progress = (conn->state != old_state) &&
                   !(conn->state == CONN_STATE_READING && !is_readable) &&
                   !(conn->state == CONN_STATE_SENDING_HEADERS && !is_writable) &&
                   !(conn->state == CONN_STATE_SENDING_BODY && !is_writable);
    }

    if (conn->state == CONN_STATE_CLOSING) {
        return CHTTP_ERR_IO;
    }

    return CHTTP_OK;
}

int connection_get_fd(const Connection* conn) { return conn ? conn->fd : -1; }

bool connection_is_closing(const Connection* conn) {
    return conn ? conn->state == CONN_STATE_CLOSING : true;
}

bool connection_is_sse(const Connection* conn) { return conn ? conn->is_sse_connection : false; }

CHttpError connection_send_reload(Connection* conn) {
    if (!conn || !conn->is_sse_connection)
        return CHTTP_ERR_INVAL;

    const char* msg = "data: reload\n\n";
    if (send(conn->fd, msg, strlen(msg), 0) < 0) {
        return CHTTP_ERR_IO;
    }
    return CHTTP_OK;
}
