#ifndef LOG_H
#define LOG_H

#include "common.h"

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
} log_level_t;

/*
 * 初始化日志：
 *  - path 为 NULL 时输出到 stderr（前台调试用）
 *  - path 为文件路径时输出到文件（守护进程用）
 */
int  log_init(const char *path, log_level_t level);
void log_close(void);

/* 内部使用，建议用下面的宏 */
void log_write(log_level_t level, const char *file, int line, const char *fmt, ...);

#define LOG_D(...) log_write(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_I(...) log_write(LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_W(...) log_write(LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_E(...) log_write(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif /* LOG_H */
