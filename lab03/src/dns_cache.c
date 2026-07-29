/*
 * dns_cache.c - Cache de respuestas DNS con expiracion por TTL.
 *
 * Tabla hash con encadenamiento, protegida por un rwlock: muchas lecturas
 * concurrentes desde los workers y escrituras esporadicas al resolver.
 */
#include "dns_cache.h"
#include "dns_parser.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define CACHE_BUCKETS 1024

typedef struct cache_entry {
    struct cache_entry *next;

    char     qname[DNS_MAX_NAME];   /* normalizado a minusculas */
    uint16_t qtype;
    uint16_t qclass;

    uint8_t  rcode;
    uint8_t  aa;

    time_t   stored_at;
    uint32_t ttl;                   /* vigencia de la entrada, en segundos */

    dns_rr_t *an; uint16_t an_count;
    dns_rr_t *ns; uint16_t ns_count;
    dns_rr_t *ar; uint16_t ar_count;
} cache_entry_t;

static cache_entry_t    *g_buckets[CACHE_BUCKETS];
static int               g_entries;
static int               g_max_entries  = 4096;
static uint32_t          g_min_ttl      = 5;
static uint32_t          g_max_ttl      = 86400;
static uint32_t          g_negative_ttl = 60;
static dns_cache_stats_t g_stats;
static pthread_rwlock_t  g_lock = PTHREAD_RWLOCK_INITIALIZER;

/* ------------------------------------------------------------------ */

static uint32_t hash_key(const char *name, uint16_t type, uint16_t rclass) {
    /* FNV-1a de 32 bits sobre el nombre normalizado + tipo + clase. */
    uint32_t h = 2166136261u;
    const unsigned char *p = (const unsigned char *)name;

    while (*p != '\0') {
        h ^= *p++;
        h *= 16777619u;
    }
    h ^= (uint32_t)type;  h *= 16777619u;
    h ^= (uint32_t)rclass; h *= 16777619u;
    return h % CACHE_BUCKETS;
}

static void entry_free(cache_entry_t *e) {
    free(e->an);
    free(e->ns);
    free(e->ar);
    free(e);
}

/* Copia una seccion a un arreglo del tamano justo. */
static dns_rr_t *dup_section(const dns_rr_t *src, uint16_t count) {
    dns_rr_t *dst;

    if (count == 0) return NULL;
    dst = malloc(sizeof(dns_rr_t) * count);
    if (dst == NULL) return NULL;
    memcpy(dst, src, sizeof(dns_rr_t) * count);
    return dst;
}

/* Desengancha una entrada de su bucket. Requiere el lock de escritura. */
static void unlink_entry(uint32_t bucket, cache_entry_t *target) {
    cache_entry_t *cur  = g_buckets[bucket];
    cache_entry_t *prev = NULL;

    while (cur != NULL) {
        if (cur == target) {
            if (prev == NULL) g_buckets[bucket] = cur->next;
            else              prev->next = cur->next;
            g_entries--;
            return;
        }
        prev = cur;
        cur  = cur->next;
    }
}

/* Elimina todas las entradas vencidas. Requiere el lock de escritura. */
static int sweep_expired(time_t now) {
    int removed = 0;
    int i;

    for (i = 0; i < CACHE_BUCKETS; i++) {
        cache_entry_t *cur  = g_buckets[i];
        cache_entry_t *prev = NULL;

        while (cur != NULL) {
            cache_entry_t *next = cur->next;

            if (now - cur->stored_at >= (time_t)cur->ttl) {
                if (prev == NULL) g_buckets[i] = next;
                else              prev->next = next;
                entry_free(cur);
                g_entries--;
                removed++;
            } else {
                prev = cur;
            }
            cur = next;
        }
    }
    return removed;
}

/* Descarta la entrada mas antigua. Requiere el lock de escritura. */
static void evict_oldest(void) {
    cache_entry_t *victim = NULL;
    uint32_t       vbucket = 0;
    int i;

    for (i = 0; i < CACHE_BUCKETS; i++) {
        cache_entry_t *cur = g_buckets[i];

        while (cur != NULL) {
            if (victim == NULL || cur->stored_at < victim->stored_at) {
                victim  = cur;
                vbucket = (uint32_t)i;
            }
            cur = cur->next;
        }
    }
    if (victim != NULL) {
        unlink_entry(vbucket, victim);
        entry_free(victim);
        g_stats.evictions++;
    }
}

/* ------------------------------------------------------------------ */

int dns_cache_init(int max_entries, uint32_t min_ttl, uint32_t max_ttl,
                   uint32_t negative_ttl) {
    memset(g_buckets, 0, sizeof g_buckets);
    memset(&g_stats, 0, sizeof g_stats);
    g_entries      = 0;
    g_max_entries  = (max_entries > 0) ? max_entries : 4096;
    g_min_ttl      = min_ttl;
    g_max_ttl      = (max_ttl > min_ttl) ? max_ttl : min_ttl + 1;
    g_negative_ttl = negative_ttl;
    return 0;
}

void dns_cache_destroy(void) {
    int i;

    pthread_rwlock_wrlock(&g_lock);
    for (i = 0; i < CACHE_BUCKETS; i++) {
        cache_entry_t *cur = g_buckets[i];

        while (cur != NULL) {
            cache_entry_t *next = cur->next;
            entry_free(cur);
            cur = next;
        }
        g_buckets[i] = NULL;
    }
    g_entries = 0;
    pthread_rwlock_unlock(&g_lock);
}

/* Copia los RRs de la entrada descontando el tiempo ya transcurrido. */
static void copy_section(const dns_rr_t *src, uint16_t count,
                         dns_rr_t *dst, uint16_t *dst_count, uint32_t age) {
    uint16_t i;

    for (i = 0; i < count && i < DNS_MAX_RR; i++) {
        dst[i] = src[i];
        dst[i].ttl = (src[i].ttl > age) ? (src[i].ttl - age) : 1;
    }
    *dst_count = i;
}

int dns_cache_get(const char *qname, uint16_t qtype, uint16_t qclass,
                  dns_message_t *out, uint8_t *rcode) {
    char           key[DNS_MAX_NAME];
    uint32_t       bucket;
    cache_entry_t *cur;
    time_t         now = time(NULL);
    int            found = 0;

    dns_name_normalize(qname, key, sizeof key);
    bucket = hash_key(key, qtype, qclass);

    pthread_rwlock_rdlock(&g_lock);
    for (cur = g_buckets[bucket]; cur != NULL; cur = cur->next) {
        if (cur->qtype != qtype || cur->qclass != qclass) continue;
        if (strcmp(cur->qname, key) != 0) continue;

        if (now - cur->stored_at >= (time_t)cur->ttl) break;  /* vencida */

        {
            uint32_t age = (uint32_t)(now - cur->stored_at);

            copy_section(cur->an, cur->an_count, out->answer,     &out->an_used, age);
            copy_section(cur->ns, cur->ns_count, out->authority,  &out->ns_used, age);
            copy_section(cur->ar, cur->ar_count, out->additional, &out->ar_used, age);
            out->header.aa = cur->aa;
            *rcode = cur->rcode;
        }
        found = 1;
        break;
    }
    pthread_rwlock_unlock(&g_lock);

    pthread_rwlock_wrlock(&g_lock);
    if (found) g_stats.hits++;
    else       g_stats.misses++;
    pthread_rwlock_unlock(&g_lock);

    return found;
}

/* TTL efectivo de la respuesta: el menor de todos sus RRs. */
static uint32_t compute_ttl(const dns_message_t *msg, uint8_t rcode) {
    uint32_t ttl = 0xFFFFFFFFu;
    uint16_t i;
    int seen = 0;

    for (i = 0; i < msg->an_used; i++) {
        if (msg->answer[i].ttl < ttl) ttl = msg->answer[i].ttl;
        seen = 1;
    }
    for (i = 0; i < msg->ns_used; i++) {
        if (msg->authority[i].ttl < ttl) ttl = msg->authority[i].ttl;
        seen = 1;
    }

    /* RFC 2308: las respuestas negativas se guardan poco tiempo. */
    if (!seen || rcode == DNS_RC_NXDOMAIN || msg->an_used == 0) {
        uint32_t neg = g_negative_ttl;

        if (seen && ttl < neg) neg = ttl;
        return neg;
    }

    if (ttl < g_min_ttl) ttl = g_min_ttl;
    if (ttl > g_max_ttl) ttl = g_max_ttl;
    return ttl;
}

void dns_cache_put(const char *qname, uint16_t qtype, uint16_t qclass,
                   const dns_message_t *msg, uint8_t rcode) {
    char           key[DNS_MAX_NAME];
    uint32_t       bucket;
    cache_entry_t *cur;
    cache_entry_t *e;
    uint32_t       ttl;
    time_t         now = time(NULL);

    /* Solo tiene sentido guardar respuestas utiles. */
    if (rcode != DNS_RC_NOERROR && rcode != DNS_RC_NXDOMAIN) return;

    ttl = compute_ttl(msg, rcode);
    if (ttl == 0) return;

    dns_name_normalize(qname, key, sizeof key);
    bucket = hash_key(key, qtype, qclass);

    e = calloc(1, sizeof *e);
    if (e == NULL) return;

    snprintf(e->qname, sizeof e->qname, "%s", key);
    e->qtype     = qtype;
    e->qclass    = qclass;
    e->rcode     = rcode;
    e->aa        = msg->header.aa;
    e->stored_at = now;
    e->ttl       = ttl;

    e->an = dup_section(msg->answer,     msg->an_used);
    e->ns = dup_section(msg->authority,  msg->ns_used);
    e->ar = dup_section(msg->additional, msg->ar_used);

    if ((msg->an_used > 0 && e->an == NULL) ||
        (msg->ns_used > 0 && e->ns == NULL) ||
        (msg->ar_used > 0 && e->ar == NULL)) {
        entry_free(e);
        return;
    }
    e->an_count = msg->an_used;
    e->ns_count = msg->ns_used;
    e->ar_count = msg->ar_used;

    pthread_rwlock_wrlock(&g_lock);

    /* Si ya existia esa clave, la nueva respuesta la reemplaza. */
    for (cur = g_buckets[bucket]; cur != NULL; cur = cur->next) {
        if (cur->qtype == qtype && cur->qclass == qclass &&
            strcmp(cur->qname, key) == 0) {
            unlink_entry(bucket, cur);
            entry_free(cur);
            break;
        }
    }

    if (g_entries >= g_max_entries) {
        int removed = sweep_expired(now);

        g_stats.expired += (unsigned long)removed;
        if (g_entries >= g_max_entries) evict_oldest();
    }

    e->next = g_buckets[bucket];
    g_buckets[bucket] = e;
    g_entries++;
    g_stats.inserts++;

    pthread_rwlock_unlock(&g_lock);
}

void dns_cache_get_stats(dns_cache_stats_t *out) {
    pthread_rwlock_rdlock(&g_lock);
    *out = g_stats;
    out->entries = g_entries;
    pthread_rwlock_unlock(&g_lock);
}
