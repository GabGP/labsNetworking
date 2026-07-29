/*
 * dns_records.h - Base de registros locales y reenviador recursivo.
 *
 * Dos responsabilidades, en el orden en que las usa el servidor:
 *
 *   1. Zona local autoritativa: registros que este servidor conoce por si
 *      mismo (A, AAAA, PTR, CNAME, MX, TXT, SOA, NS, SRV).
 *   2. Reenvio recursivo: si el nombre no es nuestro y no esta en cache,
 *      se consulta a un servidor publico (tab PROVEEDORES del enunciado).
 */
#ifndef DNS_RECORDS_H
#define DNS_RECORDS_H

#include "dns_proto.h"

/* ------------------------------------------------------------------ */
/* Zona local                                                          */
/* ------------------------------------------------------------------ */

#define ZONE_MAX_RECORDS 256
#define ZONE_MAX_ORIGINS 16

/*
 * Carga la zona local. Siempre se instalan los registros de demostracion
 * incluidos en el codigo; si `zonefile` no es NULL, sus lineas se anaden
 * encima. Devuelve el numero de registros cargados.
 */
int  zone_init(const char *zonefile);
void zone_destroy(void);

/* 1 si el nombre cae dentro de alguno de los origenes autoritativos. */
int  zone_is_authoritative(const char *name);

/*
 * Resuelve contra la zona local. Rellena las secciones de `out` y
 * devuelve el RCODE resultante, o -1 si el nombre no pertenece a la zona
 * (en cuyo caso hay que ir a cache/upstream).
 *
 * Sigue cadenas de CNAME dentro de la propia zona, como hace un servidor
 * autoritativo real.
 */
int  zone_lookup(const char *qname, uint16_t qtype, uint16_t qclass,
                 dns_message_t *out);

/* Numero de registros cargados (para el log de arranque). */
int  zone_record_count(void);

/* ------------------------------------------------------------------ */
/* Reenviador recursivo                                                */
/* ------------------------------------------------------------------ */

#define FWD_MAX_SERVERS 8

/*
 * Configura los upstreams. `servers` es un arreglo de IPs en texto
 * ("8.8.8.8", "1.1.1.1", ...). `timeout_ms` aplica a cada intento.
 */
int  fwd_init(const char servers[][64], int count, int timeout_ms);

/*
 * Reenvia la consulta y devuelve la respuesta ya decodificada.
 *
 * La consulta se construye byte a byte con nuestro propio serializador
 * (no se reenvia el paquete del cliente), y la respuesta se decodifica y
 * se vuelve a armar antes de entregarla: nunca hacemos bypass del
 * paquete original.
 *
 * Devuelve el RCODE recibido, o -1 si ningun upstream respondio.
 * `server_used` recibe la IP que contesto (puntero a memoria estatica).
 */
int  fwd_resolve(const dns_question_t *q, dns_message_t *out,
                 const char **server_used);

const char *fwd_server_list(void);

#endif /* DNS_RECORDS_H */
