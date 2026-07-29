/*
 * main.c - Punto de entrada del servidor DNS.
 *
 * Laboratorio 3 - Ciencias de la Computacion VIII
 * Implementacion completa en C sobre Debian (BeagleBone Black).
 */
#include "dns_server.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static void usage(const char *prog) {
    printf(
        "Servidor DNS sobre UDP - Laboratorio 3\n"
        "\n"
        "Uso: %s [opciones]\n"
        "\n"
        "  -a, --address IP     Direccion de escucha        (por defecto 0.0.0.0)\n"
        "  -p, --port N         Puerto UDP                  (por defecto 53)\n"
        "  -t, --threads N      Hilos del pool              (por defecto 8)\n"
        "  -q, --queue N        Tamano de la cola de tareas (por defecto 256)\n"
        "  -u, --upstream IP    Servidor DNS superior (repetible, hasta %d)\n"
        "  -w, --timeout MS     Timeout por upstream        (por defecto 3000)\n"
        "  -z, --zone ARCHIVO   Archivo de zona adicional\n"
        "  -l, --log ARCHIVO    Archivo de log              (por defecto dns_server.log)\n"
        "  -c, --cache N        Entradas maximas en cache   (por defecto 4096)\n"
        "  -n, --no-recursion   No reenviar a upstreams (solo zona local y cache)\n"
        "  -v, --verbose        Log a nivel DEBUG (muestra cada RR)\n"
        "  -h, --help           Muestra esta ayuda\n"
        "\n"
        "Ejemplos:\n"
        "  sudo %s                       # puerto 53, listo para el sistema operativo\n"
        "  %s -p 5353 -v                 # pruebas sin privilegios de root\n"
        "  sudo %s -u 1.1.1.1 -t 16      # Cloudflare como upstream, 16 hilos\n",
        prog, FWD_MAX_SERVERS, prog, prog, prog);
}

static void on_signal(int sig) {
    (void)sig;
    server_request_stop();
}

static void install_signal_handlers(void) {
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    /*
     * Sin SA_RESTART: queremos que recvfrom devuelva EINTR para que el
     * bucle principal note la senal y cierre ordenadamente.
     */
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /*
     * Un cliente que desaparece no debe tumbar el servidor con SIGPIPE
     * (puede ocurrir en el reintento por TCP hacia el upstream).
     */
    signal(SIGPIPE, SIG_IGN);
}

static int arg_matches(const char *arg, const char *shortopt, const char *longopt) {
    return strcmp(arg, shortopt) == 0 || strcmp(arg, longopt) == 0;
}

int main(int argc, char **argv) {
    server_config_t cfg;
    int i;
    int custom_upstreams = 0;

    server_config_defaults(&cfg);

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        int has_value = (i + 1 < argc);

        if (arg_matches(a, "-h", "--help")) {
            usage(argv[0]);
            return 0;
        } else if (arg_matches(a, "-v", "--verbose")) {
            cfg.verbose = 1;
        } else if (arg_matches(a, "-n", "--no-recursion")) {
            cfg.no_recursion = 1;
        } else if (arg_matches(a, "-a", "--address") && has_value) {
            snprintf(cfg.bind_addr, sizeof cfg.bind_addr, "%s", argv[++i]);
        } else if (arg_matches(a, "-p", "--port") && has_value) {
            cfg.port = atoi(argv[++i]);
        } else if (arg_matches(a, "-t", "--threads") && has_value) {
            cfg.threads = atoi(argv[++i]);
        } else if (arg_matches(a, "-q", "--queue") && has_value) {
            cfg.queue_size = atoi(argv[++i]);
        } else if (arg_matches(a, "-w", "--timeout") && has_value) {
            cfg.upstream_timeout_ms = atoi(argv[++i]);
        } else if (arg_matches(a, "-c", "--cache") && has_value) {
            cfg.cache_max = atoi(argv[++i]);
        } else if (arg_matches(a, "-z", "--zone") && has_value) {
            snprintf(cfg.zonefile, sizeof cfg.zonefile, "%s", argv[++i]);
        } else if (arg_matches(a, "-l", "--log") && has_value) {
            snprintf(cfg.logfile, sizeof cfg.logfile, "%s", argv[++i]);
        } else if (arg_matches(a, "-u", "--upstream") && has_value) {
            if (!custom_upstreams) {   /* el primer -u reemplaza los defaults */
                cfg.upstream_count = 0;
                custom_upstreams = 1;
            }
            if (cfg.upstream_count < FWD_MAX_SERVERS) {
                snprintf(cfg.upstreams[cfg.upstream_count],
                         sizeof cfg.upstreams[0], "%s", argv[++i]);
                cfg.upstream_count++;
            } else {
                fprintf(stderr, "Aviso: maximo %d upstreams, se ignora %s\n",
                        FWD_MAX_SERVERS, argv[++i]);
            }
        } else {
            fprintf(stderr, "Argumento no reconocido: %s\n\n", a);
            usage(argv[0]);
            return 1;
        }
    }

    /* Validaciones basicas para no arrancar con una configuracion absurda. */
    if (cfg.port <= 0 || cfg.port > 65535) {
        fprintf(stderr, "Puerto invalido: %d\n", cfg.port);
        return 1;
    }
    if (cfg.threads <= 0 || cfg.threads > 256)   cfg.threads = 8;
    if (cfg.queue_size <= 0)                     cfg.queue_size = 256;
    if (cfg.cache_max <= 0)                      cfg.cache_max = 4096;

    log_init(cfg.logfile[0] != '\0' ? cfg.logfile : NULL,
             cfg.verbose ? LOG_DEBUG : LOG_INFO);
    install_signal_handlers();

    if (cfg.port == 53 && geteuid() != 0) {
        LOG_W("el puerto 53 normalmente requiere root; si falla el bind use sudo "
              "o pruebe con -p 5353");
    }

    {
        int rc = server_run(&cfg);

        log_close();
        return (rc == 0) ? 0 : 1;
    }
}
