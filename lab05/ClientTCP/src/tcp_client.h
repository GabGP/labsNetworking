#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H

#include <stdint.h>

typedef struct {
    const char *local_ip;
    const char *server_ip;
    const char *message;
    const char *interface;
    uint16_t server_port;
} cli_options_t;

typedef enum {
    OPT_LOCAL,
    OPT_SERVER,
    OPT_PORT,
    OPT_MESSAGE,
    OPT_INTERFACE
} option_id_t;

typedef struct {
    const char *short_name;
    const char *long_name;
    option_id_t id;
} option_t;

void print_usage(const char *prog, const cli_options_t *defaults);
const option_t *find_option(const char *arg);
int parse_cli(int argc, char **argv, cli_options_t *opts);
int run_session(const cli_options_t *opts);

#endif