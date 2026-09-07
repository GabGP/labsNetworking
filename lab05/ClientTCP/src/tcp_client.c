/*
 * tcp_client.c
 *
 * Entry point for the raw TCP client.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tcp_client.h"
#include "tcp_session.h"

static const option_t options[] = {
    {"-l", "--local", OPT_LOCAL},
    {"-s", "--server", OPT_SERVER},
    {"-p", "--port", OPT_PORT},
    {"-m", "--message", OPT_MESSAGE},
    {"-i", "--interface", OPT_INTERFACE},
};

void print_usage(const char *prog, const cli_options_t *o)
{
    printf("\nUsage: %s [options]\n\n", prog);
    printf("  -l, --local IP       Local IP (default: %s)\n", o->local_ip);
    printf("  -s, --server IP      Server IP (default: %s)\n", o->server_ip);
    printf("  -p, --port PORT      Server port (default: %u)\n", o->server_port);
    printf("  -m, --message MSG    First message, sent without asking (default: %s)\n",
           o->message ? o->message : "none, the client always asks");
    printf("  -i, --interface IF   Network interface (default: %s)\n", o->interface ? o->interface : "not specified");
    printf("                       Linux: lo, eth0 | macOS: lo0, en0\n");
    printf("  -h, --help           Show this help\n\n");
    printf("  The client keeps ONE TCP connection open and asks for messages in a\n");
    printf("  loop. A message containing \"EXIT\" makes the server close the\n");
    printf("  connection; \"EXIT OFF\" also shuts the test server down.\n");
    printf("  Ctrl-D (EOF) closes the connection from the client side instead.\n\n");
}

const option_t *find_option(const char *arg)
{
    for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); i++)
        if (!strcmp(arg, options[i].short_name) || !strcmp(arg, options[i].long_name))
            return &options[i];

    return NULL;
}

int parse_cli(int argc, char **argv, cli_options_t *o)
{
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
            print_usage(argv[0], o);
            return 1;
        }

        const option_t *option = find_option(arg);
        if (!option) {
            fprintf(stderr, "Unknown argument: %s\n", arg);
            return -1;
        }

        if (++i >= argc) {
            fprintf(stderr, "Missing value for: %s\n", arg);
            return -1;
        }

        const char *value = argv[i];

        switch (option->id) {
        case OPT_LOCAL: o->local_ip = value; break;
        case OPT_SERVER: o->server_ip = value; break;
        case OPT_PORT: o->server_port = (uint16_t)atoi(value); break;
        case OPT_MESSAGE: o->message = value; break;
        case OPT_INTERFACE: o->interface = value; break;
        }
    }

    return 0;
}

/*
 * ServerTCP.java cierra la conexión cuando la línea recibida contiene "EXIT"
 * (String.contains, sensible a mayúsculas). Replicamos ese mismo criterio
 * para saber cuándo debemos esperar el FIN del servidor en lugar de volver a
 * pedirle un mensaje al usuario.
 */
static int is_exit_message(const char *msg)
{
    return strstr(msg, "EXIT") != NULL;
}

/*
 * Lee una línea de stdin y le quita el salto de línea final.
 * Retorna 0 si se leyó una línea, -1 si se llegó al fin de la entrada (Ctrl-D).
 */
static int read_line(char *buf, size_t buf_size)
{
    if (!fgets(buf, (int)buf_size, stdin)) return -1;

    buf[strcspn(buf, "\r\n")] = '\0';
    return 0;
}

int run_session(const cli_options_t *o)
{
    tcp_session_t session;
    char response[TCP_SESSION_MAX_MSG + 1] = {0};
    char line[TCP_SESSION_MAX_MSG + 1];

    /* Si se pasó -m, ese es el primer mensaje y no se pregunta por él; luego
       el cliente sigue pidiendo mensajes de forma interactiva. */
    const char *pending = o->message;

    if (tcp_session_open(&session, o->local_ip, o->server_ip, o->server_port, o->interface) != 0)
        return 1;

    if (tcp_session_handshake(&session, 10) != 0) {
        tcp_session_abort(&session);
        return 1;
    }

    /*
     * Una sola conexión, N mensajes: el handshake ya ocurrió y no se repite.
     * Cada vuelta del ciclo es un intercambio completo de datos
     * (PSH+ACK -> ACK -> PSH+ACK eco -> ACK) sobre la misma sesión.
     */
    for (;;) {
        const char *msg;

        if (pending) {
            msg = pending;
            pending = NULL;
            printf("\nIngrese un mensaje: %s\n", msg);
        } else {
            printf("\nIngrese un mensaje: ");
            fflush(stdout);

            if (read_line(line, sizeof(line)) != 0) {
                /* Fin de entrada: el cierre lo inicia el cliente (Opción 2). */
                printf("\n[*] Fin de entrada: el cliente inicia el cierre.\n");
                break;
            }

            if (line[0] == '\0') continue; /* enter vacío: no se envía nada */
            msg = line;
        }

        if (tcp_session_send_message(&session, msg) != 0) {
            tcp_session_abort(&session);
            return 1;
        }

        /* Espera el ACK del servidor y su eco; ambos quedan en el log. */
        tcp_rx_result_t rc = tcp_session_recv(&session, response, sizeof(response), 10);

        if (rc == TCP_RX_TIMEOUT) {
            fprintf(stderr, "[!] Timeout esperando la respuesta del servidor\n");
            break;
        }

        if (rc == TCP_RX_FIN) break; /* el servidor cerró antes de responder */

        if (is_exit_message(msg)) {
            /* El servidor cierra al recibir "EXIT": esperamos su FIN+ACK,
               que tcp_session_recv reconoce con un ACK (Opción 1). */
            if (tcp_session_recv(&session, NULL, 0, 10) != TCP_RX_FIN)
                fprintf(stderr, "[!] Timeout esperando el FIN del servidor\n");
            break;
        }
    }

    /* Nuestro FIN+ACK y el ACK final del servidor. */
    tcp_session_close(&session, 5);

    return 0;
}

int main(int argc, char **argv)
{
    cli_options_t opts = {
        .local_ip = "127.0.0.1",
        .server_ip = "127.0.0.1",
        .server_port = 5001,
        .message = NULL, /* sin -m, el cliente pide el mensaje al usuario */
        .interface = NULL,
    };

    int rc = parse_cli(argc, argv, &opts);
    if (rc != 0) return rc > 0 ? 0 : 1;

    srand((unsigned)time(NULL));
    return run_session(&opts);
}
