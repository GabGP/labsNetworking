#ifndef DNS_SERVER_H
#define DNS_SERVER_H

#include <stdint.h>
#include "dns_records.h"
#include "threadpool.h"

typedef struct {
    uint16_t port;
    char upstream_ip[64];
    uint16_t upstream_port;
    int num_threads;
    int queue_size;
    char records_file[256];
} server_config_t;

typedef struct {
    int sockfd;
    server_config_t config;
    dns_db_t db;
    threadpool_t *pool;
    volatile int running;
} dns_server_t;

int dns_server_init(dns_server_t *server, const server_config_t *config);
int dns_server_run(dns_server_t *server);
void dns_server_stop(dns_server_t *server);

#endif // DNS_SERVER_H
