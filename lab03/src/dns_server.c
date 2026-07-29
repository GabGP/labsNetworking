/*
 * dns_server.c - Socket UDP:53, despacho al pool de hilos y construccion
 * de la respuesta DNS.
 *
 * Orden de resolucion de cada consulta (el que exige el enunciado):
 *
 *   1. Zona local autoritativa  -> respuesta con AA=1
 *   2. Cache en memoria         -> respuesta propia, sin salir a la red
 *   3. Reenvio recursivo        -> se consulta al upstream y se cachea
 *
 * Solo el paso 3 sale a Internet, y solo la primera vez para cada nombre.
 */
#include "dns_server.h"
#include "dns_parser.h"
#include "dns_cache.h"
#include "dns_records.h"
#include "threadpool.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define REQ_BUF_SIZE 4096   /* suficiente para consultas, incluso con EDNS0 */

static volatile sig_atomic_t g_stop = 0;
static int                   g_sockfd = -1;
static threadpool_t         *g_pool = NULL;
static const server_config_t *g_cfg = NULL;

static unsigned long g_requests = 0;
static pthread_mutex_t g_counter_lock = PTHREAD_MUTEX_INITIALIZER;

/* Contexto de una consulta en vuelo; vive en el heap hasta que un worker
 * termina de atenderla. */
typedef struct {
    int                     fd;
    struct sockaddr_storage client;
    socklen_t               clientlen;
    size_t                  len;
    struct timespec         t0;
    uint8_t                 buf[REQ_BUF_SIZE];
} request_t;

void server_request_stop(void) {
    g_stop = 1;
    /*
     * Cerrar el socket desbloquea el recvfrom del hilo principal aunque el
     * EINTR se pierda. shutdown() es seguro desde un manejador de senales.
     */
    if (g_sockfd >= 0) shutdown(g_sockfd, SHUT_RDWR);
}

void server_config_defaults(server_config_t *cfg) {
    memset(cfg, 0, sizeof *cfg);

    snprintf(cfg->bind_addr, sizeof cfg->bind_addr, "0.0.0.0");
    cfg->port                = 53;
    cfg->threads             = 8;
    cfg->queue_size          = 256;
    cfg->upstream_timeout_ms = 3000;

    /* Tab PROVEEDORES del Laboratorio DNS.xlsx. */
    snprintf(cfg->upstreams[0], sizeof cfg->upstreams[0], "8.8.8.8");
    snprintf(cfg->upstreams[1], sizeof cfg->upstreams[1], "1.1.1.1");
    snprintf(cfg->upstreams[2], sizeof cfg->upstreams[2], "9.9.9.9");
    cfg->upstream_count = 3;

    snprintf(cfg->logfile, sizeof cfg->logfile, "dns_server.log");

    cfg->cache_max     = 4096;
    cfg->cache_min_ttl = 5;
    cfg->cache_max_ttl = 86400;
    cfg->cache_neg_ttl = 60;
}

/* ------------------------------------------------------------------ */
/* Utilidades                                                          */
/* ------------------------------------------------------------------ */

static void addr_to_text(const struct sockaddr_storage *ss, char *out, size_t outsz) {
    char ip[INET6_ADDRSTRLEN] = "?";
    int  port = 0;

    if (ss->ss_family == AF_INET) {
        const struct sockaddr_in *a = (const struct sockaddr_in *)ss;

        inet_ntop(AF_INET, &a->sin_addr, ip, sizeof ip);
        port = ntohs(a->sin_port);
    } else if (ss->ss_family == AF_INET6) {
        const struct sockaddr_in6 *a = (const struct sockaddr_in6 *)ss;

        inet_ntop(AF_INET6, &a->sin6_addr, ip, sizeof ip);
        port = ntohs(a->sin6_port);
    }
    snprintf(out, outsz, "%s:%d", ip, port);
}

static double elapsed_ms(const struct timespec *t0) {
    struct timespec t1;

    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (double)(t1.tv_sec - t0->tv_sec) * 1000.0 +
           (double)(t1.tv_nsec - t0->tv_nsec) / 1000000.0;
}

/* ------------------------------------------------------------------ */
/* Construccion de la respuesta                                        */
/* ------------------------------------------------------------------ */

/*
 * Serializa el mensaje de respuesta respetando el tamano maximo que el
 * cliente puede recibir. Si no caben todos los RRs, se emiten los que
 * quepan y se marca TC=1 para que el cliente reintente por TCP.
 */
static size_t build_response(uint8_t *out, size_t maxlen,
                             const dns_header_t *qhdr,
                             const dns_question_t *question, int has_question,
                             const dns_message_t *data, uint8_t rcode,
                             int aa, int client_edns, uint16_t edns_payload) {
    dns_writer_t w;
    dns_header_t h;
    uint16_t an = 0, ns = 0, ar = 0;
    uint16_t i;
    int      truncated = 0;

    memset(&h, 0, sizeof h);
    h.id     = qhdr->id;
    h.qr     = 1;
    h.opcode = qhdr->opcode;
    h.aa     = (uint8_t)(aa ? 1 : 0);
    h.tc     = 0;
    h.rd     = qhdr->rd;
    h.ra     = 1;                   /* este servidor si ofrece recursion */
    h.rcode  = rcode;

    dns_writer_init(&w, out, maxlen);
    if (dns_write_header(&w, &h) < 0) return 0;

    if (has_question) {
        if (dns_write_question(&w, question) < 0) return 0;
    }

    if (data != NULL) {
        for (i = 0; i < data->an_used; i++) {
            if (dns_write_rr(&w, &data->answer[i]) < 0) { truncated = 1; break; }
            an++;
        }
        if (!truncated) {
            for (i = 0; i < data->ns_used; i++) {
                if (dns_write_rr(&w, &data->authority[i]) < 0) { truncated = 1; break; }
                ns++;
            }
        }
        if (!truncated) {
            for (i = 0; i < data->ar_used; i++) {
                if (dns_write_rr(&w, &data->additional[i]) < 0) { truncated = 1; break; }
                ar++;
            }
        }
    }

    /* EDNS0: solo se devuelve OPT si el cliente lo ofrecio (RFC 6891). */
    if (client_edns) {
        if (dns_write_opt(&w, edns_payload) == 0) ar++;
    }

    dns_patch_counts(&w, (uint16_t)(has_question ? 1 : 0), an, ns, ar);
    dns_patch_tc(&w, truncated);

    return w.len;
}

/* Respuesta minima de error: cabecera + pregunta (si se pudo leer). */
static size_t build_error(uint8_t *out, size_t maxlen, const dns_header_t *qhdr,
                          const dns_question_t *q, int has_question,
                          uint8_t rcode) {
    return build_response(out, maxlen, qhdr, q, has_question, NULL, rcode, 0, 0, 0);
}

static void log_answers(const dns_message_t *msg) {
    char text[512];
    uint16_t i;

    for (i = 0; i < msg->an_used; i++) {
        dns_rr_to_text(&msg->answer[i], text, sizeof text);
        LOG_D("    ANSWER  %s %u %s %s", msg->answer[i].name,
              msg->answer[i].ttl, dns_type_name(msg->answer[i].type), text);
    }
    for (i = 0; i < msg->ns_used; i++) {
        dns_rr_to_text(&msg->authority[i], text, sizeof text);
        LOG_D("    AUTH    %s %u %s %s", msg->authority[i].name,
              msg->authority[i].ttl, dns_type_name(msg->authority[i].type), text);
    }
    for (i = 0; i < msg->ar_used; i++) {
        dns_rr_to_text(&msg->additional[i], text, sizeof text);
        LOG_D("    ADDL    %s %u %s %s", msg->additional[i].name,
              msg->additional[i].ttl, dns_type_name(msg->additional[i].type), text);
    }
}

/* ------------------------------------------------------------------ */
/* Manejo de una consulta (se ejecuta en un worker del pool)           */
/* ------------------------------------------------------------------ */

static void handle_request(void *arg) {
    request_t     *req = (request_t *)arg;
    dns_message_t *query  = NULL;
    dns_message_t *result = NULL;
    uint8_t        resp[DNS_MAX_MSG];
    char           peer[80];
    const char    *source = "-";
    const char    *upstream = NULL;
    size_t         resplen = 0;
    size_t         maxresp = DNS_MAX_UDP;
    uint8_t        rcode = DNS_RC_NOERROR;
    int            aa = 0;
    int            parsed;
    uint16_t       txid = 0;    /* se usa en el log incluso si el parseo falla */

    addr_to_text(&req->client, peer, sizeof peer);

    query  = malloc(sizeof *query);
    result = malloc(sizeof *result);
    if (query == NULL || result == NULL) {
        LOG_E("sin memoria para atender la consulta de %s", peer);
        goto cleanup;
    }
    memset(result, 0, sizeof *result);

    parsed = dns_parse_message(req->buf, req->len, query);

    if (parsed != 0) {
        /*
         * Paquete malformado. Si al menos entra la cabecera se responde
         * FORMERR conservando el Transaction ID; si ni eso, se descarta en
         * silencio (responder a basura solo amplifica el trafico).
         */
        dns_header_t hdr;

        if (dns_parse_header(req->buf, req->len, &hdr) != 0) {
            LOG_W("> DROP   cliente=%s bytes=%zu (paquete demasiado corto)",
                  peer, req->len);
            goto cleanup;
        }
        txid  = hdr.id;
        rcode = DNS_RC_FORMERR;
        LOG_W("> QUERY  id=0x%04x cliente=%s bytes=%zu -> FORMERR (paquete malformado)",
              hdr.id, peer, req->len);
        resplen = build_error(resp, maxresp, &hdr, NULL, 0, DNS_RC_FORMERR);
        goto send;
    }

    txid = query->header.id;

    /* Una respuesta no es una consulta: se ignora. */
    if (query->header.qr != 0) {
        LOG_W("> DROP   id=0x%04x cliente=%s (QR=1, no es una consulta)",
              query->header.id, peer);
        goto cleanup;
    }

    /* Tamano maximo de respuesta segun EDNS0 del cliente. */
    if (query->has_opt) {
        uint16_t p = query->opt_payload;

        if (p < DNS_MAX_UDP) p = DNS_MAX_UDP;
        if (p > 4096)        p = 4096;
        maxresp = p;
    }

    LOG_I("> QUERY  id=0x%04x cliente=%s nombre=%s tipo=%s clase=%s rd=%u edns=%s",
          query->header.id, peer,
          query->has_question ? query->question.qname : "(sin pregunta)",
          dns_type_name(query->question.qtype),
          dns_class_name(query->question.qclass),
          query->header.rd,
          query->has_opt ? "si" : "no");

    if (!query->has_question || query->header.qdcount != 1) {
        rcode = DNS_RC_FORMERR;
        resplen = build_error(resp, maxresp, &query->header, NULL, 0, rcode);
        goto send;
    }
    if (query->header.opcode != DNS_OP_QUERY) {
        rcode = DNS_RC_NOTIMP;
        resplen = build_error(resp, maxresp, &query->header, &query->question, 1, rcode);
        goto send;
    }
    if (query->question.qclass != DNS_CLASS_IN &&
        query->question.qclass != DNS_CLASS_ANY) {
        rcode = DNS_RC_REFUSED;
        resplen = build_error(resp, maxresp, &query->header, &query->question, 1, rcode);
        goto send;
    }

    /* ---- 1. Zona local ------------------------------------------- */
    {
        int zrc = zone_lookup(query->question.qname, query->question.qtype,
                              query->question.qclass, result);

        if (zrc >= 0) {
            rcode  = (uint8_t)zrc;
            source = "LOCAL";
            aa     = 1;
            goto respond;
        }
    }

    /* ---- 2. Cache ------------------------------------------------ */
    memset(result, 0, sizeof *result);
    if (dns_cache_get(query->question.qname, query->question.qtype,
                      query->question.qclass, result, &rcode)) {
        source = "CACHE";
        aa     = 0;
        goto respond;
    }

    /* ---- 3. Reenvio recursivo ------------------------------------ */
    if (g_cfg->no_recursion || !query->header.rd) {
        /*
         * Sin recursion no podemos contestar por un dominio ajeno: se
         * responde REFUSED en vez de mentir con una respuesta vacia.
         */
        memset(result, 0, sizeof *result);
        rcode  = DNS_RC_REFUSED;
        source = "NO-REC";
        goto respond;
    }

    memset(result, 0, sizeof *result);
    {
        int frc = fwd_resolve(&query->question, result, &upstream);

        if (frc < 0) {
            memset(result, 0, sizeof *result);
            rcode  = DNS_RC_SERVFAIL;
            source = "UPSTREAM";
            LOG_W("  fallo la resolucion recursiva de %s", query->question.qname);
            goto respond;
        }

        rcode  = (uint8_t)frc;
        source = "UPSTREAM";
        aa     = 0;

        /* Se guarda para que la proxima consulta salga de nuestra cache. */
        dns_cache_put(query->question.qname, query->question.qtype,
                      query->question.qclass, result, rcode);
    }

respond:
    resplen = build_response(resp, maxresp, &query->header, &query->question, 1,
                             result, rcode, aa,
                             query->has_opt, (uint16_t)maxresp);
    log_answers(result);

send:
    if (resplen == 0) {
        LOG_E("no se pudo construir la respuesta para %s", peer);
        goto cleanup;
    }

    if (sendto(req->fd, resp, resplen, 0,
               (struct sockaddr *)&req->client, req->clientlen) < 0) {
        LOG_E("< ERROR  dst=%s: fallo el envio: %s", peer, strerror(errno));
        goto cleanup;
    }

    LOG_I("< REPLY  id=0x%04x cliente=%s rcode=%s origen=%s%s%s aa=%d an=%u ns=%u ar=%u "
          "bytes=%zu tc=%d tiempo=%.3fms",
          txid, peer,
          dns_rcode_name(rcode), source,
          (upstream != NULL) ? "@" : "", (upstream != NULL) ? upstream : "",
          aa,
          (result != NULL) ? result->an_used : 0,
          (result != NULL) ? result->ns_used : 0,
          (result != NULL) ? result->ar_used : 0,
          resplen, (resp[2] & 0x02) ? 1 : 0, elapsed_ms(&req->t0));

cleanup:
    free(query);
    free(result);
    free(req);

    pthread_mutex_lock(&g_counter_lock);
    g_requests++;
    if (g_requests % 100 == 0) {
        dns_cache_stats_t cs;
        unsigned long sub, comp, rej;
        int queued;

        pthread_mutex_unlock(&g_counter_lock);
        dns_cache_get_stats(&cs);
        tp_stats(g_pool, &sub, &comp, &rej, &queued);
        LOG_I("  estado: consultas=%lu cache(hits=%lu misses=%lu entradas=%d) "
              "pool(encoladas=%d rechazadas=%lu)",
              g_requests, cs.hits, cs.misses, cs.entries, queued, rej);
    } else {
        pthread_mutex_unlock(&g_counter_lock);
    }
}

/* ------------------------------------------------------------------ */
/* Bucle principal                                                     */
/* ------------------------------------------------------------------ */

static int open_socket(const server_config_t *cfg) {
    struct sockaddr_in addr;
    int fd;
    int opt = 1;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        LOG_E("no se pudo crear el socket UDP: %s", strerror(errno));
        return -1;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt) < 0) {
        LOG_W("setsockopt(SO_REUSEADDR) fallo: %s", strerror(errno));
    }

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)cfg->port);
    if (inet_pton(AF_INET, cfg->bind_addr, &addr.sin_addr) != 1) {
        LOG_E("direccion de escucha invalida: %s", cfg->bind_addr);
        close(fd);
        return -1;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        LOG_E("no se pudo enlazar %s:%d: %s", cfg->bind_addr, cfg->port,
              strerror(errno));
        if (errno == EACCES) {
            LOG_E("el puerto 53 requiere privilegios: ejecute con sudo");
        } else if (errno == EADDRINUSE) {
            LOG_E("el puerto ya esta ocupado (systemd-resolved suele usar el 53)");
        }
        close(fd);
        return -1;
    }

    return fd;
}

int server_run(const server_config_t *cfg) {
    int rc = 0;

    g_cfg = cfg;
    log_set_thread_name("main");

    zone_init(cfg->zonefile[0] != '\0' ? cfg->zonefile : NULL);
    dns_cache_init(cfg->cache_max, cfg->cache_min_ttl, cfg->cache_max_ttl,
                   cfg->cache_neg_ttl);
    fwd_init(cfg->upstreams, cfg->upstream_count, cfg->upstream_timeout_ms);

    g_sockfd = open_socket(cfg);
    if (g_sockfd < 0) return -1;

    g_pool = tp_create(cfg->threads, cfg->queue_size);
    if (g_pool == NULL) {
        LOG_E("no se pudo crear el pool de hilos");
        close(g_sockfd);
        g_sockfd = -1;
        return -1;
    }

    LOG_I("====================================================");
    LOG_I(" Servidor DNS sobre UDP - Laboratorio 3 (C / POSIX)");
    LOG_I("====================================================");
    LOG_I(" escuchando en    : %s:%d/udp", cfg->bind_addr, cfg->port);
    LOG_I(" hilos del pool   : %d (cola de %d)", cfg->threads, cfg->queue_size);
    LOG_I(" zona local       : %d registros%s%s", zone_record_count(),
          cfg->zonefile[0] ? " + archivo " : "", cfg->zonefile);
    LOG_I(" upstreams        : %s (timeout %d ms)", fwd_server_list(),
          cfg->upstream_timeout_ms);
    LOG_I(" recursion        : %s", cfg->no_recursion ? "deshabilitada" : "habilitada");
    LOG_I(" cache            : %d entradas, TTL %u-%u s, negativo %u s",
          cfg->cache_max, cfg->cache_min_ttl, cfg->cache_max_ttl,
          cfg->cache_neg_ttl);
    LOG_I(" log              : %s", cfg->logfile[0] ? cfg->logfile : "(solo consola)");
    LOG_I("----------------------------------------------------");

    while (!g_stop) {
        request_t *req = malloc(sizeof *req);
        ssize_t    n;

        if (req == NULL) {
            LOG_E("sin memoria para recibir; se reintenta");
            usleep(100000);
            continue;
        }

        req->fd        = g_sockfd;
        req->clientlen = sizeof req->client;

        n = recvfrom(g_sockfd, req->buf, sizeof req->buf, 0,
                     (struct sockaddr *)&req->client, &req->clientlen);
        if (n < 0) {
            free(req);
            if (errno == EINTR) continue;          /* llego una senal */
            if (g_stop) break;
            LOG_E("recvfrom fallo: %s", strerror(errno));
            continue;
        }
        if (n == 0) {                              /* socket cerrado */
            free(req);
            if (g_stop) break;
            continue;
        }

        req->len = (size_t)n;
        clock_gettime(CLOCK_MONOTONIC, &req->t0);

        if (tp_submit(g_pool, handle_request, req) < 0) {
            char peer[80];

            addr_to_text(&req->client, peer, sizeof peer);
            LOG_W("cola llena, se descarta la consulta de %s", peer);
            free(req);
        }
    }

    LOG_I("----------------------------------------------------");
    LOG_I("deteniendo el servidor...");

    close(g_sockfd);
    g_sockfd = -1;
    tp_destroy(g_pool);
    g_pool = NULL;

    {
        dns_cache_stats_t cs;

        dns_cache_get_stats(&cs);
        LOG_I("total de consultas atendidas : %lu", g_requests);
        LOG_I("cache: hits=%lu misses=%lu inserciones=%lu expiradas=%lu "
              "desalojadas=%lu entradas=%d",
              cs.hits, cs.misses, cs.inserts, cs.expired, cs.evictions, cs.entries);
    }

    dns_cache_destroy();
    zone_destroy();
    LOG_I("servidor detenido correctamente");
    return rc;
}
