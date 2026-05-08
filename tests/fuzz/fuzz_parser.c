#include <stddef.h>
#include <stdint.h>

#include "http_parser.h"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    HttpRequest* req;
    if (http_request_create(&req) != CHTTP_OK) {
        return 0;
    }

    size_t parsed = 0;
    http_request_parse(req, (const char*)data, size, &parsed);

    http_request_destroy(req);
    return 0;
}
