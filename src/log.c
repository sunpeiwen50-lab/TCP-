#include "log.h"
#include <stdarg.h>
#include <time.h>
#include <pthread.h>

static FILE           *g_fp     = NULL;
static log_level_t     g_level  = LOG_INFO;
static pthread_mutex_t g_lock   = PTHREAD_MUTEX_INITIALIZER;

static const char *level_str[] = { "DEBUG", "INFO", "WARN", "ERROR" };

int log_init(const char *path, log_level_t level)
{
    g_level = level;
    if (path == NULL) {
        g_fp = stderr;
    } else {
        g_fp = fopen(path, "a");
        if (!g_fp) {
            g_fp = stderr;
            return -1;
        }
        /* 行缓冲，保证日志及时落盘 */
        setvbuf(g_fp, NULL, _IOLBF, 0);
    }
    return 0;
}

void log_close(void)
{
    if (g_fp && g_fp != stderr) {
        fclose(g_fp);
        g_fp = NULL;
    }
}

void log_write(log_level_t level, const char *file, int line, const char *fmt, ...)
{
    if (level < g_level || !g_fp) return;

    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm);

    /* 只取文件名，不要整个路径 */
    const char *base = strrchr(file, '/');
    base = base ? base + 1 : file;

    pthread_mutex_lock(&g_lock);
    fprintf(g_fp, "[%s][%-5s][%s:%d] ", timebuf, level_str[level], base, line);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_fp, fmt, ap);
    va_end(ap);
    fprintf(g_fp, "\n");
    pthread_mutex_unlock(&g_lock);
}
