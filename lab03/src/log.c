/*
 * log.c - Implementacion del logger estructurado.
 */
#include "log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/time.h>

static FILE           *g_file      = NULL;
static log_level_t     g_level     = LOG_INFO;
static pthread_mutex_t g_mutex     = PTHREAD_MUTEX_INITIALIZER;

/* Cada hilo lleva su propia etiqueta ("main", "worker-3", ...). */
static __thread char   g_thread_name[24] = "main";

static const char *LEVEL_TAG[] = { "DEBUG", "INFO ", "WARN ", "ERROR" };

int log_init(const char *path, log_level_t level) {
    g_level = level;

    if (path == NULL) return 0;

    g_file = fopen(path, "a");
    if (g_file == NULL) {
        fprintf(stderr, "[WARN ] no se pudo abrir el log '%s': %s\n",
                path, strerror(errno));
        return -1;
    }
    return 0;
}

void log_set_thread_name(const char *name) {
    snprintf(g_thread_name, sizeof g_thread_name, "%s", name);
}

void log_close(void) {
    pthread_mutex_lock(&g_mutex);
    if (g_file != NULL) {
        fclose(g_file);
        g_file = NULL;
    }
    pthread_mutex_unlock(&g_mutex);
}

void log_write(log_level_t level, const char *fmt, ...) {
    char            line[2048];
    char            stamp[32];
    struct timeval  tv;
    struct tm       tm_buf;
    va_list         ap;
    int             n;

    if (level < g_level) return;

    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm_buf);
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tm_buf);

    n = snprintf(line, sizeof line, "%s.%03ld [%s] [%s] ",
                 stamp, (long)(tv.tv_usec / 1000),
                 LEVEL_TAG[level], g_thread_name);
    if (n < 0 || (size_t)n >= sizeof line) return;

    va_start(ap, fmt);
    vsnprintf(line + n, sizeof line - (size_t)n, fmt, ap);
    va_end(ap);

    pthread_mutex_lock(&g_mutex);
    fprintf(stdout, "%s\n", line);
    fflush(stdout);
    if (g_file != NULL) {
        fprintf(g_file, "%s\n", line);
        fflush(g_file);
    }
    pthread_mutex_unlock(&g_mutex);
}
