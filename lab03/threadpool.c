#include "threadpool.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

typedef struct {
    threadpool_t *pool;
    task_handler_fn handler;
    void *user_arg;
} worker_arg_t;

static void *threadpool_worker(void *arg) {
    worker_arg_t *warg = (worker_arg_t *)arg;
    threadpool_t *pool = warg->pool;
    task_handler_fn handler = warg->handler;
    void *user_arg = warg->user_arg;
    free(warg);

    while (1) {
        dns_task_t task;

        pthread_mutex_lock(&(pool->lock));

        while ((pool->count == 0) && (!pool->shutdown)) {
            pthread_cond_wait(&(pool->notify), &(pool->lock));
        }

        if (pool->shutdown && pool->count == 0) {
            pthread_mutex_unlock(&(pool->lock));
            pthread_exit(NULL);
        }

        // Dequeue task
        task = pool->queue[pool->head];
        pool->head = (pool->head + 1) % pool->queue_size;
        pool->count--;

        pthread_mutex_unlock(&(pool->lock));

        // Execute task handler
        if (handler != NULL) {
            handler(&task, user_arg);
        }
    }

    return NULL;
}

threadpool_t *threadpool_create(int thread_count, int queue_size, task_handler_fn handler, void *user_arg) {
    if (thread_count <= 0 || queue_size <= 0 || handler == NULL) {
        return NULL;
    }

    threadpool_t *pool = (threadpool_t *)malloc(sizeof(threadpool_t));
    if (pool == NULL) {
        return NULL;
    }

    pool->thread_count = thread_count;
    pool->queue_size = queue_size;
    pool->head = 0;
    pool->tail = 0;
    pool->count = 0;
    pool->shutdown = 0;

    pool->queue = (dns_task_t *)malloc(sizeof(dns_task_t) * queue_size);
    pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * thread_count);

    if (pool->queue == NULL || pool->threads == NULL) {
        if (pool->queue) free(pool->queue);
        if (pool->threads) free(pool->threads);
        free(pool);
        return NULL;
    }

    if (pthread_mutex_init(&(pool->lock), NULL) != 0 ||
        pthread_cond_init(&(pool->notify), NULL) != 0) {
        free(pool->queue);
        free(pool->threads);
        free(pool);
        return NULL;
    }

    for (int i = 0; i < thread_count; i++) {
        worker_arg_t *warg = (worker_arg_t *)malloc(sizeof(worker_arg_t));
        warg->pool = pool;
        warg->handler = handler;
        warg->user_arg = user_arg;
        if (pthread_create(&(pool->threads[i]), NULL, threadpool_worker, (void *)warg) != 0) {
            threadpool_destroy(pool);
            return NULL;
        }
    }

    return pool;
}

int threadpool_add(threadpool_t *pool, const dns_task_t *task) {
    if (pool == NULL || task == NULL) {
        return -1;
    }

    pthread_mutex_lock(&(pool->lock));

    if (pool->shutdown) {
        pthread_mutex_unlock(&(pool->lock));
        return -1;
    }

    if (pool->count == pool->queue_size) {
        // Queue full
        pthread_mutex_unlock(&(pool->lock));
        return -1;
    }

    pool->queue[pool->tail] = *task;
    pool->tail = (pool->tail + 1) % pool->queue_size;
    pool->count++;

    pthread_cond_signal(&(pool->notify));
    pthread_mutex_unlock(&(pool->lock));

    return 0;
}

int threadpool_destroy(threadpool_t *pool) {
    if (pool == NULL) {
        return -1;
    }

    pthread_mutex_lock(&(pool->lock));

    if (pool->shutdown) {
        pthread_mutex_unlock(&(pool->lock));
        return -1;
    }

    pool->shutdown = 1;

    pthread_cond_broadcast(&(pool->notify));
    pthread_mutex_unlock(&(pool->lock));

    for (int i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    pthread_mutex_destroy(&(pool->lock));
    pthread_cond_destroy(&(pool->notify));

    free(pool->queue);
    free(pool->threads);
    free(pool);

    return 0;
}
