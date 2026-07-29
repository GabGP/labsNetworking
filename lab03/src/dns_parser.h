/*
 * dns_parser.h - Serializacion / deserializacion binaria de mensajes DNS.
 *
 * Cubre la cabecera de 12 bytes, la seccion Question, los Resource Records
 * y la compresion de nombres por punteros (prefijo 0xC0) descrita en el
 * RFC 1035 seccion 4.1.4 y en el tab "MENSAJE COMPRIMIDO" del enunciado.
 */
#ifndef DNS_PARSER_H
#define DNS_PARSER_H

#include "dns_proto.h"

/* ------------------------------------------------------------------ */
/* Lectura                                                             */
/* ------------------------------------------------------------------ */

/*
 * Decodifica un datagrama DNS completo.
 * Devuelve 0 si el mensaje es valido, o un DNS_RC_* (>0) con el codigo de
 * error que corresponde responder (FORMERR ante paquetes malformados).
 *
 * El parser nunca lee fuera de `buf` y aborta ante bucles de punteros de
 * compresion, por lo que es seguro alimentarlo con datos de la red.
 */
int dns_parse_message(const uint8_t *buf, size_t len, dns_message_t *msg);

/* Decodifica solo la cabecera; util para descartar paquetes cortos. */
int dns_parse_header(const uint8_t *buf, size_t len, dns_header_t *hdr);

/*
 * Lee un nombre de dominio en `buf` a partir de `start` y lo escribe en
 * `out` en formato presentacion ("dns.google.", la raiz es ".").
 * Sigue punteros de compresion y escapa caracteres especiales al estilo
 * RFC 1035 (\. y \DDD) para que el nombre pueda re-serializarse intacto.
 *
 * `consumed` recibe los bytes ocupados en el flujo original (un puntero
 * de compresion siempre ocupa 2 bytes, sin importar a donde salte).
 * Devuelve 0 en exito, -1 si el nombre esta malformado.
 */
int dns_parse_name(const uint8_t *buf, size_t len, size_t start,
                   char *out, size_t outsz, size_t *consumed);

/* ------------------------------------------------------------------ */
/* Escritura                                                           */
/* ------------------------------------------------------------------ */

#define DNS_CTABLE_MAX 64   /* nombres recordados para comprimir */

typedef struct {
    char     name[DNS_MAX_NAME];
    uint16_t offset;
} dns_cname_entry_t;

/*
 * Constructor incremental de mensajes DNS.
 *
 * Mantiene la tabla de nombres ya escritos para poder emitir punteros de
 * compresion (0xC0 | offset) en lugar de repetir el dominio completo.
 */
typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   len;
    int      overflow;          /* 1 si alguna escritura no cupo */

    dns_cname_entry_t ctable[DNS_CTABLE_MAX];
    int      ctable_used;
} dns_writer_t;

void dns_writer_init(dns_writer_t *w, uint8_t *buf, size_t cap);

/* Escribe la cabecera al inicio del buffer (reserva los 12 bytes). */
int dns_write_header(dns_writer_t *w, const dns_header_t *hdr);

/* Reescribe los contadores de la cabecera ya emitida (backfill final). */
void dns_patch_counts(dns_writer_t *w, uint16_t qd, uint16_t an,
                      uint16_t ns, uint16_t ar);

/* Marca el bit TC en la cabecera ya emitida. */
void dns_patch_tc(dns_writer_t *w, int tc);

int dns_write_question(dns_writer_t *w, const dns_question_t *q);

/*
 * Serializa un RR completo. Si no cabe en el buffer, deja el writer tal
 * como estaba (rollback) y devuelve -1 para que el llamador trunque.
 */
int dns_write_rr(dns_writer_t *w, const dns_rr_t *rr);

/* Anade el pseudo-RR OPT de EDNS0 con el tamano de payload indicado. */
int dns_write_opt(dns_writer_t *w, uint16_t payload);

/*
 * Escribe un nombre de dominio. Con `compress` distinto de cero puede
 * emitir un puntero a una aparicion previa del mismo sufijo.
 */
int dns_write_name(dns_writer_t *w, const char *name, int compress);

/* ------------------------------------------------------------------ */
/* Utilidades                                                          */
/* ------------------------------------------------------------------ */

/* Compara nombres de dominio ignorando mayusculas (DNS es case-insensitive). */
int dns_name_equal(const char *a, const char *b);

/* Normaliza a minusculas y garantiza el punto final. */
void dns_name_normalize(const char *in, char *out, size_t outsz);

/* Convierte texto a RDATA para tipos conocidos; -1 si el valor es invalido. */
int dns_rr_set_from_text(dns_rr_t *rr, uint16_t type, const char *value);

/* Representacion legible del RDATA para los logs. */
void dns_rr_to_text(const dns_rr_t *rr, char *out, size_t outsz);

#endif /* DNS_PARSER_H */
