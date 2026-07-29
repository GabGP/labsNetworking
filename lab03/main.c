#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "dns_server.h"

static dns_server_t g_server;

static void handle_signal(int sig) {
    (void)sig;
    dns_server_stop(&g_server);
    exit(0);
}

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -p <port>        Port to listen on (default: 53)\n");
    printf("  -u <ip>          Upstream DNS server IP for recursive queries (default: 8.8.8.8)\n");
    printf("  -P <port>        Upstream DNS server port (default: 53)\n");
    printf("  -t <threads>     Number of worker threads in pool (default: 4)\n");
    printf("  -q <qsize>       Maximum task queue size (default: 100)\n");
    printf("  -z <file>        Path to custom zone records configuration file\n");
    printf("  -h               Show this help message\n");
}

int main(int argc, char *argv[]) {
    server_config_t config;
    config.port = 53;
    strncpy(config.upstream_ip, "8.8.8.8", sizeof(config.upstream_ip) - 1);
    config.upstream_port = 53;
    config.num_threads = 4;
    config.queue_size = 100;
    config.records_file[0] = '\0';

    int opt;
    while ((opt = getopt(argc, argv, "p:u:P:t:q:z:h")) != -1) {
        switch (opt) {
            case 'p':
                config.port = (uint16_t)atoi(optarg);
                break;
            case 'u':
                strncpy(config.upstream_ip, optarg, sizeof(config.upstream_ip) - 1);
                break;
            case 'P':
                config.upstream_port = (uint16_t)atoi(optarg);
                break;
            case 't':
                config.num_threads = atoi(optarg);
                break;
            case 'q':
                config.queue_size = atoi(optarg);
                break;
            case 'z':
                strncpy(config.records_file, optarg, sizeof(config.records_file) - 1);
                break;
            case 'h':
            default:
                print_usage(argv[0]);
                return (opt == 'h') ? 0 : 1;
        }
    }

    // Set up signal handlers for graceful termination
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (dns_server_init(&g_server, &config) != 0) {
        fprintf(stderr, "[-] Server initialization failed.\n");
        return 1;
    }

    dns_server_run(&g_server);

    return 0;
}
