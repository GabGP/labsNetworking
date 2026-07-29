/*
 * log.h - Logger estructurado con salida simultanea a consola y archivo.
 *
 * Formato de cada linea:
 *   YYYY-MM-DD HH:MM:SS.mmm [NIVEL] [hilo] mensaje
 *
 * Es la misma idea del LOGGER de archivos del Laboratorio #2, reescrita en
 * C y protegida con un mutex para que varios hilos del pool no entrelacen
 * sus lineas.
 */
#ifndef LOG_H
#define LOG_H

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3
} log_level_t;

/*
 * Inicializa el logger. `path` puede ser NULL para escribir solo a consola.
 * Devuelve 0 en exito, -1 si el archivo no se pudo abrir (la consola sigue
 * funcionando, el servidor nunca se detiene por esto).
 */
int  log_init(const char *path, log_level_t level);

/* Etiqueta el hilo actual para que aparezca en sus lineas de log. */
void log_set_thread_name(const char *name);

void log_close(void);

void log_write(log_level_t level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#define LOG_D(...) log_write(LOG_DEBUG, __VA_ARGS__)
#define LOG_I(...) log_write(LOG_INFO,  __VA_ARGS__)
#define LOG_W(...) log_write(LOG_WARN,  __VA_ARGS__)
#define LOG_E(...) log_write(LOG_ERROR, __VA_ARGS__)

#endif /* LOG_H */
