/*
 * dns_cache.h - Cache de respuestas en memoria con expiracion por TTL.
 *
 * El enunciado exige que "todas las respuestas salgan de su laboratorio si
 * existen": una vez resuelto un nombre, las siguientes consultas se
 * contestan desde aqui y no se vuelve a molestar al servidor upstream
 * hasta que expire el TTL.
 *
 * La cache es puramente volatil: al reiniciar el servidor arranca vacia,
 * tal como pide el tab "Implementacion" del Laboratorio DNS.xlsx.
 */
#ifndef DNS_CACHE_H
#define DNS_CACHE_H

#include "dns_proto.h"

typedef struct {
    unsigned long hits;
    unsigned long misses;
    unsigned long inserts;
    unsigned long evictions;
    unsigned long expired;
    int           entries;
} dns_cache_stats_t;

int  dns_cache_init(int max_entries, uint32_t min_ttl, uint32_t max_ttl,
                    uint32_t negative_ttl);
void dns_cache_destroy(void);

/*
 * Busca (qname, qtype, qclass). Si hay acierto vigente devuelve 1, copia
 * las secciones en `out` con los TTL ya descontados por el tiempo
 * transcurrido y deja el RCODE original en `rcode`. Devuelve 0 si no hay
 * nada utilizable.
 */
int  dns_cache_get(const char *qname, uint16_t qtype, uint16_t qclass,
                   dns_message_t *out, uint8_t *rcode);

/*
 * Guarda una respuesta. El TTL de la entrada es el menor TTL de sus RRs,
 * acotado por los limites de configuracion. Las respuestas negativas
 * (NXDOMAIN o sin answers) se guardan con `negative_ttl` segun RFC 2308.
 */
void dns_cache_put(const char *qname, uint16_t qtype, uint16_t qclass,
                   const dns_message_t *msg, uint8_t rcode);

void dns_cache_get_stats(dns_cache_stats_t *out);

#endif /* DNS_CACHE_H */
