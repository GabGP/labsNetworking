/*
 * threadpool.h - Pool de hilos POSIX con cola acotada.
 *
 * Equivalente en C puro del ThreadPool usado en los laboratorios previos:
 * N hilos trabajadores consumen tareas de una cola circular protegida por
 * un pthread_mutex_t y dos variables de condicion (hay trabajo / hay sitio).
 */
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <stddef.h>

typedef void (*tp_job_fn)(void *arg);

typedef struct {
    tp_job_fn fn;
    void     *arg;
} tp_job_t;

typedef struct {
    pthread_t      *threads;
    int             nthreads;

    tp_job_t       *queue;
    int             qcap;
    int             qhead;
    int             qtail;
    int             qcount;

    pthread_mutex_t lock;
    pthread_cond_t  not_empty;   /* despierta a los workers        */
    pthread_cond_t  not_full;    /* despierta al hilo productor    */

    int             shutdown;

    /* Contadores para el log de estado. */
    unsigned long   submitted;
    unsigned long   completed;
    unsigned long   rejected;
} threadpool_t;

/*
 * Crea el pool con `nthreads` trabajadores y una cola de `qcap` tareas.
 * Devuelve NULL si algun recurso no se pudo reservar.
 */
threadpool_t *tp_create(int nthreads, int qcap);

/*
 * Encola una tarea. Si la cola esta llena devuelve -1 de inmediato en vez
 * de bloquear: ante una avalancha de consultas UDP es preferible descartar
 * y seguir atendiendo que dejar de leer el socket.
 */
int tp_submit(threadpool_t *tp, tp_job_fn fn, void *arg);

/* Espera a que los hilos terminen la tarea en curso y libera todo. */
void tp_destroy(threadpool_t *tp);

/* Instantanea de contadores (para logs periodicos o de cierre). */
void tp_stats(threadpool_t *tp, unsigned long *submitted,
              unsigned long *completed, unsigned long *rejected,
              int *queued);

#endif /* THREADPOOL_H */
