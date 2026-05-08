#ifndef CHTTP_CONNECTION_H
#define CHTTP_CONNECTION_H

#include <sys/types.h>

#include "../include/chttp.h"
#include "error.h"
#include "http_parser.h"
#include "http_response.h"

typedef enum {
    CONN_STATE_READING,
    CONN_STATE_PARSING,
    CONN_STATE_HANDLING,
    CONN_STATE_SENDING_HEADERS,
    CONN_STATE_SENDING_BODY,
    CONN_STATE_CLOSING
} ConnectionState;

typedef struct Connection Connection;
CHttpError connection_create(int fd, const CHttpIoOps* io_ops, Connection** out_conn);
void connection_destroy(Connection* conn);
CHttpError connection_process(Connection* conn, bool is_readable, bool is_writable);

int connection_get_fd(const Connection* conn);
bool connection_is_closing(const Connection* conn);
bool connection_is_sse(const Connection* conn);
CHttpError connection_send_reload(Connection* conn);

#endif
