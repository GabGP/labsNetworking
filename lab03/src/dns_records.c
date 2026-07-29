/*
 * dns_records.c - Zona local autoritativa + reenviador recursivo.
 */
#include "dns_records.h"
#include "dns_parser.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ================================================================== */
/* Parte 1: zona local                                                 */
/* ================================================================== */

static dns_rr_t g_zone[ZONE_MAX_RECORDS];
static int      g_zone_count;

static char     g_origins[ZONE_MAX_ORIGINS][DNS_MAX_NAME];
static int      g_origin_count;

static int origin_add(const char *origin) {
    if (g_origin_count >= ZONE_MAX_ORIGINS) return -1;
    dns_name_normalize(origin, g_origins[g_origin_count],
                       sizeof g_origins[g_origin_count]);
    g_origin_count++;
    return 0;
}

/* Devuelve el origen que cubre `name`, o NULL si no es nuestro. */
static const char *origin_for(const char *name) {
    char   norm[DNS_MAX_NAME];
    size_t nlen;
    int    i;

    dns_name_normalize(name, norm, sizeof norm);
    nlen = strlen(norm);

    for (i = 0; i < g_origin_count; i++) {
        size_t olen = strlen(g_origins[i]);

        if (nlen < olen) continue;
        if (strcmp(norm + (nlen - olen), g_origins[i]) != 0) continue;
        /* El sufijo debe empezar en un limite de etiqueta. */
        if (nlen == olen || norm[nlen - olen - 1] == '.') return g_origins[i];
    }
    return NULL;
}

int zone_is_authoritative(const char *name) {
    return origin_for(name) != NULL;
}

static int zone_add(const char *name, uint32_t ttl, uint16_t type,
                    const char *value) {
    dns_rr_t *rr;

    if (g_zone_count >= ZONE_MAX_RECORDS) {
        LOG_W("zona llena (%d registros), se ignora %s", ZONE_MAX_RECORDS, name);
        return -1;
    }

    rr = &g_zone[g_zone_count];
    memset(rr, 0, sizeof *rr);
    dns_name_normalize(name, rr->name, sizeof rr->name);
    rr->rclass = DNS_CLASS_IN;
    rr->ttl    = ttl;

    if (dns_rr_set_from_text(rr, type, value) < 0) {
        LOG_W("registro invalido: %s %s %s", name, dns_type_name(type), value);
        return -1;
    }

    g_zone_count++;
    return 0;
}

/* Registros de demostracion; reproducen los ejemplos del enunciado. */
static void zone_load_defaults(void) {
    origin_add("lab.local.");
    origin_add("1.168.192.in-addr.arpa.");

    zone_add("lab.local.", 3600, DNS_TYPE_SOA,
             "ns1.lab.local. admin.lab.local. 2026072901 7200 3600 1209600 3600");
    zone_add("lab.local.", 3600, DNS_TYPE_NS,    "ns1.lab.local.");
    zone_add("lab.local.", 3600, DNS_TYPE_A,     "192.168.1.10");
    zone_add("lab.local.", 3600, DNS_TYPE_MX,    "10 mail.lab.local.");
    zone_add("lab.local.", 3600, DNS_TYPE_TXT,
             "\"v=spf1 include:_spf.google.com ~all\"");

    zone_add("ns1.lab.local.",  3600, DNS_TYPE_A,    "192.168.1.10");
    zone_add("www.lab.local.",  3600, DNS_TYPE_A,    "192.168.1.100");
    zone_add("www.lab.local.",  3600, DNS_TYPE_AAAA,
             "2001:0db8:85a3:0000:0000:8a2e:0370:7334");
    zone_add("mail.lab.local.", 3600, DNS_TYPE_A,    "192.168.1.25");
    zone_add("bbb.lab.local.",  3600, DNS_TYPE_A,    "192.168.1.50");
    zone_add("bbb.lab.local.",  3600, DNS_TYPE_AAAA, "fe80::1a2b:3cff:fe4d:5e6f");

    /* Alias que ejercita el seguimiento de CNAME dentro de la zona. */
    zone_add("alias.lab.local.", 3600, DNS_TYPE_CNAME, "www.lab.local.");

    zone_add("_sip._udp.lab.local.", 3600, DNS_TYPE_SRV, "10 5 5060 mail.lab.local.");

    /* Zona inversa: PTR para las direcciones de arriba. */
    zone_add("1.168.192.in-addr.arpa.", 3600, DNS_TYPE_SOA,
             "ns1.lab.local. admin.lab.local. 2026072901 7200 3600 1209600 3600");
    zone_add("1.168.192.in-addr.arpa.", 3600, DNS_TYPE_NS, "ns1.lab.local.");
    zone_add("10.1.168.192.in-addr.arpa.",  3600, DNS_TYPE_PTR, "ns1.lab.local.");
    zone_add("100.1.168.192.in-addr.arpa.", 3600, DNS_TYPE_PTR, "www.lab.local.");
    zone_add("25.1.168.192.in-addr.arpa.",  3600, DNS_TYPE_PTR, "mail.lab.local.");
    zone_add("50.1.168.192.in-addr.arpa.",  3600, DNS_TYPE_PTR, "bbb.lab.local.");
}

/*
 * Formato del archivo de zona (una directiva o registro por linea):
 *
 *   # comentario
 *   ZONE  lab.local.
 *   www.lab.local.   3600  A      192.168.1.100
 *   lab.local.       3600  TXT    "texto con espacios"
 */
static int zone_load_file(const char *path) {
    FILE *f = fopen(path, "r");
    char  line[512];
    int   loaded = 0;
    int   lineno = 0;

    if (f == NULL) {
        LOG_W("no se pudo abrir el archivo de zona '%s': %s", path, strerror(errno));
        return 0;
    }

    while (fgets(line, sizeof line, f) != NULL) {
        char     name[DNS_MAX_NAME], typestr[16];
        unsigned ttl;
        char    *p = line;
        int      consumed = 0;
        uint16_t type;

        lineno++;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == ';' || *p == '\n' || *p == '\0') continue;

        if (strncasecmp(p, "ZONE", 4) == 0 && (p[4] == ' ' || p[4] == '\t')) {
            char origin[DNS_MAX_NAME];

            if (sscanf(p + 4, "%255s", origin) == 1) {
                origin_add(origin);
                LOG_I("zona: origen autoritativo %s", origin);
            }
            continue;
        }

        if (sscanf(p, "%255s %u %15s %n", name, &ttl, typestr, &consumed) < 3) {
            LOG_W("zona: linea %d ignorada (formato invalido)", lineno);
            continue;
        }

        type = dns_type_from_name(typestr);
        if (type == 0) {
            LOG_W("zona: linea %d, tipo desconocido '%s'", lineno, typestr);
            continue;
        }

        {
            char *value = p + consumed;
            size_t vlen;

            while (*value == ' ' || *value == '\t') value++;
            vlen = strlen(value);
            while (vlen > 0 && (value[vlen - 1] == '\n' || value[vlen - 1] == '\r' ||
                                value[vlen - 1] == ' '  || value[vlen - 1] == '\t')) {
                value[--vlen] = '\0';
            }
            if (vlen == 0) {
                LOG_W("zona: linea %d sin valor", lineno);
                continue;
            }
            if (zone_add(name, ttl, type, value) == 0) loaded++;
        }
    }

    fclose(f);
    return loaded;
}

int zone_init(const char *zonefile) {
    g_zone_count   = 0;
    g_origin_count = 0;

    zone_load_defaults();
    if (zonefile != NULL) {
        int extra = zone_load_file(zonefile);
        LOG_I("zona: %d registros adicionales desde '%s'", extra, zonefile);
    }
    return g_zone_count;
}

void zone_destroy(void) {
    g_zone_count   = 0;
    g_origin_count = 0;
}

int zone_record_count(void) {
    return g_zone_count;
}

/* Copia a `dst` los registros de `name` cuyo tipo coincide. */
static int zone_collect(const char *name, uint16_t type,
                        dns_rr_t *dst, uint16_t *count, uint16_t max) {
    int added = 0;
    int i;

    for (i = 0; i < g_zone_count; i++) {
        if (*count >= max) break;
        if (!dns_name_equal(g_zone[i].name, name)) continue;
        if (type != DNS_TYPE_ANY && g_zone[i].type != type) continue;

        dst[*count] = g_zone[i];
        (*count)++;
        added++;
    }
    return added;
}

/* 1 si el nombre existe en la zona con cualquier tipo. */
static int zone_name_exists(const char *name) {
    int i;

    for (i = 0; i < g_zone_count; i++) {
        if (dns_name_equal(g_zone[i].name, name)) return 1;
    }
    return 0;
}

/* Anade el SOA del origen correspondiente a la seccion Authority. */
static void zone_add_soa(const char *name, dns_message_t *out) {
    const char *origin = origin_for(name);

    if (origin == NULL) return;
    zone_collect(origin, DNS_TYPE_SOA, out->authority, &out->ns_used, DNS_MAX_RR);
}

/*
 * Glue: para respuestas que apuntan a otro nombre (NS, MX, SRV, CNAME,
 * PTR) se adjuntan sus direcciones en la seccion Additional.
 */
static void zone_add_glue(const dns_rr_t *rr, dns_message_t *out) {
    const char *target = NULL;

    switch (rr->type) {
        case DNS_TYPE_NS:
        case DNS_TYPE_CNAME:
        case DNS_TYPE_PTR:  target = rr->rd.name;        break;
        case DNS_TYPE_MX:   target = rr->rd.mx.exchange; break;
        case DNS_TYPE_SRV:  target = rr->rd.srv.target;  break;
        default: return;
    }
    if (!rr->decoded || target == NULL) return;

    zone_collect(target, DNS_TYPE_A,    out->additional, &out->ar_used, DNS_MAX_RR);
    zone_collect(target, DNS_TYPE_AAAA, out->additional, &out->ar_used, DNS_MAX_RR);
}

int zone_lookup(const char *qname, uint16_t qtype, uint16_t qclass,
                dns_message_t *out) {
    char name[DNS_MAX_NAME];
    int  hops = 0;

    if (qclass != DNS_CLASS_IN && qclass != DNS_CLASS_ANY) return -1;

    dns_name_normalize(qname, name, sizeof name);
    if (!zone_is_authoritative(name)) return -1;

    out->header.aa = 1;

    /* Coincidencia directa por tipo. */
    if (zone_collect(name, qtype, out->answer, &out->an_used, DNS_MAX_RR) > 0) {
        uint16_t i;
        uint16_t n = out->an_used;

        for (i = 0; i < n; i++) zone_add_glue(&out->answer[i], out);
        return DNS_RC_NOERROR;
    }

    /*
     * Sin coincidencia directa: puede haber un CNAME. Se sigue la cadena
     * dentro de la zona, igual que un servidor autoritativo real.
     */
    while (qtype != DNS_TYPE_CNAME && hops < 8) {
        dns_rr_t cname;
        uint16_t before = out->an_used;

        if (zone_collect(name, DNS_TYPE_CNAME, out->answer,
                         &out->an_used, DNS_MAX_RR) == 0) break;

        cname = out->answer[before];
        if (!cname.decoded) break;

        snprintf(name, sizeof name, "%s", cname.rd.name);
        hops++;

        /* El destino puede estar fuera de la zona: ahi se corta la cadena. */
        if (!zone_is_authoritative(name)) return DNS_RC_NOERROR;

        if (zone_collect(name, qtype, out->answer, &out->an_used, DNS_MAX_RR) > 0)
            return DNS_RC_NOERROR;
    }

    if (out->an_used > 0) return DNS_RC_NOERROR;

    /*
     * Nada que responder. Se distingue entre "el nombre existe pero no con
     * ese tipo" (NODATA: NOERROR con 0 answers) y "el nombre no existe"
     * (NXDOMAIN). En ambos casos va el SOA en Authority (RFC 2308).
     */
    zone_add_soa(name, out);
    return zone_name_exists(name) ? DNS_RC_NOERROR : DNS_RC_NXDOMAIN;
}

/* ================================================================== */
/* Parte 2: reenviador recursivo                                       */
/* ================================================================== */

static char g_servers[FWD_MAX_SERVERS][64];
static int  g_server_count;
static int  g_timeout_ms = 3000;
static char g_server_list[FWD_MAX_SERVERS * 20];

/* Semilla por hilo para los Transaction ID salientes. */
static __thread unsigned g_seed;
static __thread int      g_seed_ready;

static uint16_t next_txid(void) {
    if (!g_seed_ready) {
        g_seed = (unsigned)time(NULL) ^ (unsigned)(uintptr_t)pthread_self();
        g_seed_ready = 1;
    }
    return (uint16_t)(rand_r(&g_seed) & 0xFFFF);
}

int fwd_init(const char servers[][64], int count, int timeout_ms) {
    int i;
    size_t used = 0;

    g_server_count = 0;
    g_timeout_ms   = (timeout_ms > 0) ? timeout_ms : 3000;
    g_server_list[0] = '\0';

    for (i = 0; i < count && g_server_count < FWD_MAX_SERVERS; i++) {
        struct in_addr  v4;
        struct in6_addr v6;

        if (inet_pton(AF_INET, servers[i], &v4) != 1 &&
            inet_pton(AF_INET6, servers[i], &v6) != 1) {
            LOG_W("upstream invalido, se ignora: %s", servers[i]);
            continue;
        }
        snprintf(g_servers[g_server_count], sizeof g_servers[0], "%s", servers[i]);
        g_server_count++;

        used += (size_t)snprintf(g_server_list + used,
                                 sizeof g_server_list - used,
                                 "%s%s", (used > 0) ? ", " : "", servers[i]);
    }
    return g_server_count;
}

const char *fwd_server_list(void) {
    return (g_server_count > 0) ? g_server_list : "(ninguno)";
}

/* Construye la consulta saliente con nuestro propio serializador. */
static int build_query(uint8_t *buf, size_t cap, uint16_t txid,
                       const dns_question_t *q, size_t *out_len) {
    dns_writer_t w;
    dns_header_t h;

    memset(&h, 0, sizeof h);
    h.id      = txid;
    h.qr      = 0;
    h.opcode  = DNS_OP_QUERY;
    h.rd      = 1;              /* pedimos recursion al upstream */
    h.qdcount = 1;
    h.arcount = 1;              /* el pseudo-RR OPT de EDNS0     */

    dns_writer_init(&w, buf, cap);
    if (dns_write_header(&w, &h) < 0) return -1;
    if (dns_write_question(&w, q) < 0) return -1;
    if (dns_write_opt(&w, 4096) < 0) return -1;

    *out_len = w.len;
    return 0;
}

/* Verifica que la respuesta corresponde a la consulta que enviamos. */
static int response_matches(const dns_message_t *msg, uint16_t txid,
                            const dns_question_t *q) {
    if (msg->header.id != txid) return 0;
    if (!msg->header.qr) return 0;
    if (!msg->has_question) return 0;
    if (msg->question.qtype != q->qtype) return 0;
    if (msg->question.qclass != q->qclass) return 0;
    return dns_name_equal(msg->question.qname, q->qname);
}

static int fill_sockaddr(const char *ip, struct sockaddr_storage *ss,
                         socklen_t *slen) {
    memset(ss, 0, sizeof *ss);

    if (strchr(ip, ':') != NULL) {
        struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)ss;

        a6->sin6_family = AF_INET6;
        a6->sin6_port   = htons(53);
        if (inet_pton(AF_INET6, ip, &a6->sin6_addr) != 1) return -1;
        *slen = sizeof *a6;
    } else {
        struct sockaddr_in *a4 = (struct sockaddr_in *)ss;

        a4->sin_family = AF_INET;
        a4->sin_port   = htons(53);
        if (inet_pton(AF_INET, ip, &a4->sin_addr) != 1) return -1;
        *slen = sizeof *a4;
    }
    return 0;
}

static void set_timeout(int fd, int ms) {
    struct timeval tv;

    tv.tv_sec  = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

/*
 * Reintento por TCP cuando la respuesta UDP llega truncada (bit TC).
 * Sobre TCP el mensaje va precedido por su longitud en 2 bytes.
 */
static int query_tcp(const char *ip, const uint8_t *query, size_t qlen,
                     uint8_t *resp, size_t respcap) {
    struct sockaddr_storage ss;
    socklen_t slen;
    int      fd;
    uint8_t  lenbuf[2];
    uint16_t rlen;
    size_t   got = 0;
    ssize_t  n;

    if (fill_sockaddr(ip, &ss, &slen) < 0) return -1;

    fd = socket(ss.ss_family, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    set_timeout(fd, g_timeout_ms);

    if (connect(fd, (struct sockaddr *)&ss, slen) < 0) { close(fd); return -1; }

    lenbuf[0] = (uint8_t)(qlen >> 8);
    lenbuf[1] = (uint8_t)(qlen & 0xFF);
    if (send(fd, lenbuf, 2, 0) != 2)              { close(fd); return -1; }
    if (send(fd, query, qlen, 0) != (ssize_t)qlen) { close(fd); return -1; }

    if (recv(fd, lenbuf, 2, MSG_WAITALL) != 2)    { close(fd); return -1; }
    rlen = (uint16_t)((lenbuf[0] << 8) | lenbuf[1]);
    if (rlen == 0 || rlen > respcap)              { close(fd); return -1; }

    while (got < rlen) {
        n = recv(fd, resp + got, rlen - got, 0);
        if (n <= 0) { close(fd); return -1; }
        got += (size_t)n;
    }

    close(fd);
    return (int)got;
}

int fwd_resolve(const dns_question_t *q, dns_message_t *out,
                const char **server_used) {
    uint8_t  query[DNS_MAX_UDP];
    uint8_t  resp[DNS_MAX_MSG];
    size_t   qlen = 0;
    int      attempt;

    if (g_server_count == 0) return -1;

    for (attempt = 0; attempt < g_server_count; attempt++) {
        const char *ip = g_servers[attempt];
        struct sockaddr_storage ss;
        socklen_t slen;
        uint16_t  txid = next_txid();
        int       fd;
        ssize_t   n;
        int       parsed;

        if (build_query(query, sizeof query, txid, q, &qlen) < 0) return -1;
        if (fill_sockaddr(ip, &ss, &slen) < 0) continue;

        fd = socket(ss.ss_family, SOCK_DGRAM, 0);
        if (fd < 0) {
            LOG_W("upstream %s: no se pudo crear socket: %s", ip, strerror(errno));
            continue;
        }
        set_timeout(fd, g_timeout_ms);

        if (sendto(fd, query, qlen, 0, (struct sockaddr *)&ss, slen) < 0) {
            LOG_W("upstream %s: fallo el envio: %s", ip, strerror(errno));
            close(fd);
            continue;
        }

        n = recvfrom(fd, resp, sizeof resp, 0, NULL, NULL);
        close(fd);

        if (n < 0) {
            LOG_W("upstream %s: sin respuesta en %d ms", ip, g_timeout_ms);
            continue;
        }

        parsed = dns_parse_message(resp, (size_t)n, out);
        if (parsed != 0) {
            LOG_W("upstream %s: respuesta malformada", ip);
            continue;
        }
        if (!response_matches(out, txid, q)) {
            /* ID o pregunta distintos: posible respuesta falsificada. */
            LOG_W("upstream %s: respuesta descartada (no coincide la consulta)", ip);
            continue;
        }

        /* Truncada: se repite la consulta sobre TCP para traerla completa. */
        if (out->header.tc) {
            int tn = query_tcp(ip, query, qlen, resp, sizeof resp);

            if (tn > 0) {
                dns_message_t tcp_msg;

                if (dns_parse_message(resp, (size_t)tn, &tcp_msg) == 0 &&
                    response_matches(&tcp_msg, txid, q)) {
                    *out = tcp_msg;
                    LOG_D("upstream %s: respuesta completada por TCP (%d bytes)",
                          ip, tn);
                }
            }
        }

        if (server_used != NULL) *server_used = ip;
        return out->header.rcode;
    }

    return -1;
}
