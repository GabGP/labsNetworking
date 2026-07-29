#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <netinet/in.h>
#include <sys/time.h>

#define MAX_DNS_PACKET_SIZE 512

typedef struct {
    struct sockaddr_in client_addr;
    socklen_t addr_len;
    uint8_t buffer[MAX_DNS_PACKET_SIZE];
    size_t length;
    struct timeval receive_time;
} dns_task_t;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t notify;
    pthread_t *threads;
    dns_task_t *queue;
    int thread_count;
    int queue_size;
    int head;
    int tail;
    int count;
    int shutdown;
} threadpool_t;

typedef void (*task_handler_fn)(dns_task_t *task, void *arg);

threadpool_t *threadpool_create(int thread_count, int queue_size, task_handler_fn handler, void *user_arg);
int threadpool_add(threadpool_t *pool, const dns_task_t *task);
int threadpool_destroy(threadpool_t *pool);

#endif // THREADPOOL_H
