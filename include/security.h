#ifndef SECURITY_H
#define SECURITY_H

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline int safe_snprintf(char* str, size_t size, const char* format, ...) {
    if (!str || size == 0)
        return -1;
    va_list args;
    va_start(args, format);
    int ret = __builtin___vsnprintf_chk(str, size, 0, __builtin_object_size(str, 0), format, args);
    va_end(args);
    return ret;
}

static inline void safe_memcpy(void* dest, size_t dest_size, const void* src, size_t n) {
    if (dest && src && n <= dest_size) {
        __builtin___memmove_chk(dest, src, n, __builtin_object_size(dest, 0));
    }
}

static inline void safe_puts(FILE* stream, const char* str) {
    if (stream && str) {
        if (fputs(str, stream) == EOF) {
            perror("Critical I/O error during safe_puts");
            abort();
        }
    }
}

#endif
