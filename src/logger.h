#ifndef CHTTP_LOGGER_H
#define CHTTP_LOGGER_H

typedef enum { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR } LogLevel;

void logger_set_level(LogLevel level);
void logger_log(LogLevel level, const char* file, int line, const char* fmt, ...);

#define LOG_DEBUG(...) logger_log(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) logger_log(LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...) logger_log(LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) logger_log(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif
