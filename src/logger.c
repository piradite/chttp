#include "logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "../include/security.h"

static LogLevel g_log_level = LOG_INFO;

void logger_set_level(LogLevel level) { g_log_level = level; }

void logger_log(LogLevel level, const char* file, int line, const char* fmt, ...) {
    if (level < g_log_level) {
        return;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm* tm_info = localtime(&tv.tv_sec);

    char time_buf[26];
    // NOLINTNEXTLINE
    (void)strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    const char* level_strs[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    const char* level_str = (level <= LOG_ERROR) ? level_strs[level] : "UNKNOWN";

    (void)fputs("[", stderr);
    (void)fputs(time_buf, stderr);
    (void)fputs(".", stderr);
    char ms_buf[8];
    (void)safe_snprintf(ms_buf, sizeof(ms_buf), "%03d", (int)(tv.tv_usec / 1000));
    (void)fputs(ms_buf, stderr);
    (void)fputs("] [", stderr);
    (void)fputs(level_str, stderr);
    (void)fputs("] ", stderr);
    (void)fputs(file, stderr);
    (void)fputs(":", stderr);
    char line_buf[16];
    (void)safe_snprintf(line_buf, sizeof(line_buf), "%d", line);
    (void)fputs(line_buf, stderr);
    (void)fputs(": ", stderr);

    va_list args;
    va_start(args, fmt);
    (void)vfprintf(stderr, fmt, args);
    va_end(args);

    (void)fputc('\n', stderr);
}
