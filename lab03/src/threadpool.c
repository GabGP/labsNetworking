/*
 * threadpool.c - Pool de hilos con cola circular acotada.
 */
#include "threadpool.h"
#include "log.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    threadpool_t *tp;
    int           index;
} worker_ctx_t;

static void *worker_main(void *arg) {
    worker_ctx_t *ctx = (worker_ctx_t *)arg;
    threadpool_t *tp  = ctx->tp;
    char          name[24];

    snprintf(name, sizeof name, "worker-%d", ctx->index);
    log_set_thread_name(name);
    free(ctx);

    for (;;) {
        tp_job_t job;

        pthread_mutex_lock(&tp->lock);
        while (tp->qcount == 0 && !tp->shutdown) {
            pthread_cond_wait(&tp->not_empty, &tp->lock);
        }
        if (tp->qcount == 0 && tp->shutdown) {
            pthread_mutex_unlock(&tp->lock);
            break;
        }

        job = tp->queue[tp->qhead];
        tp->qhead = (tp->qhead + 1) % tp->qcap;
        tp->qcount--;
        pthread_cond_signal(&tp->not_full);
        pthread_mutex_unlock(&tp->lock);

        job.fn(job.arg);

        pthread_mutex_lock(&tp->lock);
        tp->completed++;
        pthread_mutex_unlock(&tp->lock);
    }

    LOG_D("worker detenido");
    return NULL;
}

threadpool_t *tp_create(int nthreads, int qcap) {
    threadpool_t *tp;
    int i;

    if (nthreads <= 0 || qcap <= 0) return NULL;

    tp = calloc(1, sizeof *tp);
    if (tp == NULL) return NULL;

    tp->threads = calloc((size_t)nthreads, sizeof *tp->threads);
    tp->queue   = calloc((size_t)qcap, sizeof *tp->queue);
    if (tp->threads == NULL || tp->queue == NULL) {
        free(tp->threads);
        free(tp->queue);
        free(tp);
        return NULL;
    }

    tp->nthreads = nthreads;
    tp->qcap     = qcap;

    if (pthread_mutex_init(&tp->lock, NULL) != 0) {
        free(tp->threads); free(tp->queue); free(tp);
        return NULL;
    }
    pthread_cond_init(&tp->not_empty, NULL);
    pthread_cond_init(&tp->not_full, NULL);

    for (i = 0; i < nthreads; i++) {
        worker_ctx_t *ctx = malloc(sizeof *ctx);

        if (ctx == NULL) break;
        ctx->tp    = tp;
        ctx->index = i + 1;

        if (pthread_create(&tp->threads[i], NULL, worker_main, ctx) != 0) {
            free(ctx);
            break;
        }
    }

    if (i == 0) {           /* ni un solo hilo pudo arrancar */
        tp->nthreads = 0;
        tp_destroy(tp);
        return NULL;
    }
    tp->nthreads = i;       /* pudieron ser menos de los pedidos */
    return tp;
}

int tp_submit(threadpool_t *tp, tp_job_fn fn, void *arg) {
    if (tp == NULL || fn == NULL) return -1;

    pthread_mutex_lock(&tp->lock);

    if (tp->shutdown) {
        pthread_mutex_unlock(&tp->lock);
        return -1;
    }
    if (tp->qcount == tp->qcap) {
        /* Cola saturada: se rechaza sin bloquear el hilo receptor. */
        tp->rejected++;
        pthread_mutex_unlock(&tp->lock);
        return -1;
    }

    tp->queue[tp->qtail].fn  = fn;
    tp->queue[tp->qtail].arg = arg;
    tp->qtail = (tp->qtail + 1) % tp->qcap;
    tp->qcount++;
    tp->submitted++;

    pthread_cond_signal(&tp->not_empty);
    pthread_mutex_unlock(&tp->lock);
    return 0;
}

void tp_destroy(threadpool_t *tp) {
    int i;

    if (tp == NULL) return;

    pthread_mutex_lock(&tp->lock);
    tp->shutdown = 1;
    pthread_cond_broadcast(&tp->not_empty);
    pthread_cond_broadcast(&tp->not_full);
    pthread_mutex_unlock(&tp->lock);

    for (i = 0; i < tp->nthreads; i++) {
        pthread_join(tp->threads[i], NULL);
    }

    pthread_mutex_destroy(&tp->lock);
    pthread_cond_destroy(&tp->not_empty);
    pthread_cond_destroy(&tp->not_full);

    free(tp->threads);
    free(tp->queue);
    free(tp);
}

void tp_stats(threadpool_t *tp, unsigned long *submitted,
              unsigned long *completed, unsigned long *rejected,
              int *queued) {
    if (tp == NULL) return;

    pthread_mutex_lock(&tp->lock);
    if (submitted != NULL) *submitted = tp->submitted;
    if (completed != NULL) *completed = tp->completed;
    if (rejected  != NULL) *rejected  = tp->rejected;
    if (queued    != NULL) *queued    = tp->qcount;
    pthread_mutex_unlock(&tp->lock);
}
