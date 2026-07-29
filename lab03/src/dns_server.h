/*
 * dns_server.h - Motor UDP del servidor y configuracion en tiempo de
 * ejecucion.
 */
#ifndef DNS_SERVER_H
#define DNS_SERVER_H

#include <stdint.h>
#include "dns_records.h"

typedef struct {
    char     bind_addr[64];                  /* 0.0.0.0 por defecto        */
    int      port;                           /* 53                         */
    int      threads;                        /* workers del pool           */
    int      queue_size;                     /* tareas encoladas maximas   */
    int      upstream_timeout_ms;

    char     upstreams[FWD_MAX_SERVERS][64];
    int      upstream_count;

    char     zonefile[256];                  /* "" = solo zona interna     */
    char     logfile[256];

    int      cache_max;
    uint32_t cache_min_ttl;
    uint32_t cache_max_ttl;
    uint32_t cache_neg_ttl;

    int      verbose;                        /* 1 = nivel DEBUG            */
    int      no_recursion;                   /* 1 = solo responder local   */
} server_config_t;

/* Valores por defecto razonables para la BeagleBone Black. */
void server_config_defaults(server_config_t *cfg);

/*
 * Abre el socket UDP, levanta el pool y atiende consultas hasta recibir
 * SIGINT/SIGTERM. Devuelve 0 en salida limpia, -1 si no pudo arrancar.
 */
int  server_run(const server_config_t *cfg);

/* Invocable desde un manejador de senales. */
void server_request_stop(void);

#endif /* DNS_SERVER_H */
