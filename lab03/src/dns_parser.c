/*
 * dns_parser.c - Codificacion y decodificacion binaria del protocolo DNS.
 *
 * Todo el manejo de bytes crudos vive aqui: cabecera de 12 bytes, seccion
 * Question, Resource Records de cada seccion y la compresion de nombres
 * mediante punteros con prefijo 0xC0 (RFC 1035, 4.1.4).
 */
#include "dns_parser.h"

#include <string.h>
#include <strings.h>    /* strcasecmp */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <arpa/inet.h>

/* ------------------------------------------------------------------ */
/* Lectura/escritura de enteros en orden de red (big endian)           */
/* ------------------------------------------------------------------ */

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t rd32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* ------------------------------------------------------------------ */
/* Tablas de nombres legibles                                          */
/* ------------------------------------------------------------------ */

static const struct { uint16_t type; const char *name; } TYPE_NAMES[] = {
    { DNS_TYPE_A,     "A"     }, { DNS_TYPE_NS,    "NS"    },
    { DNS_TYPE_CNAME, "CNAME" }, { DNS_TYPE_SOA,   "SOA"   },
    { DNS_TYPE_PTR,   "PTR"   }, { DNS_TYPE_MX,    "MX"    },
    { DNS_TYPE_TXT,   "TXT"   }, { DNS_TYPE_AAAA,  "AAAA"  },
    { DNS_TYPE_SRV,   "SRV"   }, { DNS_TYPE_OPT,   "OPT"   },
    { DNS_TYPE_SVCB,  "SVCB"  }, { DNS_TYPE_HTTPS, "HTTPS" },
    { DNS_TYPE_CAA,   "CAA"   }, { DNS_TYPE_ANY,   "ANY"   },
    { 0, NULL }
};

const char *dns_type_name(uint16_t type) {
    static __thread char tmp[16];
    int i;
    for (i = 0; TYPE_NAMES[i].name != NULL; i++) {
        if (TYPE_NAMES[i].type == type) return TYPE_NAMES[i].name;
    }
    /* RFC 3597: los tipos desconocidos se muestran como TYPEnnn. */
    snprintf(tmp, sizeof tmp, "TYPE%u", type);
    return tmp;
}

uint16_t dns_type_from_name(const char *name) {
    int i;
    for (i = 0; TYPE_NAMES[i].name != NULL; i++) {
        if (strcasecmp(TYPE_NAMES[i].name, name) == 0) return TYPE_NAMES[i].type;
    }
    return 0;
}

const char *dns_class_name(uint16_t rclass) {
    static __thread char tmp[16];
    switch (rclass) {
        case DNS_CLASS_IN:  return "IN";
        case DNS_CLASS_CS:  return "CS";
        case DNS_CLASS_CH:  return "CH";
        case DNS_CLASS_HS:  return "HS";
        case DNS_CLASS_ANY: return "ANY";
        default:
            snprintf(tmp, sizeof tmp, "CLASS%u", rclass);
            return tmp;
    }
}

const char *dns_rcode_name(uint8_t rcode) {
    static __thread char tmp[16];
    switch (rcode) {
        case DNS_RC_NOERROR:  return "NOERROR";
        case DNS_RC_FORMERR:  return "FORMERR";
        case DNS_RC_SERVFAIL: return "SERVFAIL";
        case DNS_RC_NXDOMAIN: return "NXDOMAIN";
        case DNS_RC_NOTIMP:   return "NOTIMP";
        case DNS_RC_REFUSED:  return "REFUSED";
        default:
            snprintf(tmp, sizeof tmp, "RCODE%u", rcode);
            return tmp;
    }
}

const char *dns_opcode_name(uint8_t opcode) {
    static __thread char tmp[16];
    switch (opcode) {
        case DNS_OP_QUERY:  return "QUERY";
        case DNS_OP_IQUERY: return "IQUERY";
        case DNS_OP_STATUS: return "STATUS";
        case DNS_OP_NOTIFY: return "NOTIFY";
        case DNS_OP_UPDATE: return "UPDATE";
        default:
            snprintf(tmp, sizeof tmp, "OPCODE%u", opcode);
            return tmp;
    }
}

/* ------------------------------------------------------------------ */
/* Nombres de dominio                                                  */
/* ------------------------------------------------------------------ */

int dns_name_equal(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

void dns_name_normalize(const char *in, char *out, size_t outsz) {
    size_t i = 0;

    if (in == NULL || in[0] == '\0' || (in[0] == '.' && in[1] == '\0')) {
        snprintf(out, outsz, ".");
        return;
    }
    while (in[i] != '\0' && i + 2 < outsz) {
        out[i] = (char)tolower((unsigned char)in[i]);
        i++;
    }
    if (i > 0 && out[i - 1] != '.') out[i++] = '.';
    out[i] = '\0';
}

int dns_parse_name(const uint8_t *buf, size_t len, size_t start,
                   char *out, size_t outsz, size_t *consumed) {
    size_t pos = start, outlen = 0, used = 0;
    int jumps = 0, jumped = 0;

    if (outsz < 2) return -1;

    for (;;) {
        uint8_t l;

        if (pos >= len) return -1;
        l = buf[pos];

        if ((l & 0xC0) == 0xC0) {
            /* Puntero de compresion: 2 bytes, 14 bits de offset. */
            uint16_t ptr;

            if (pos + 1 >= len) return -1;
            ptr = (uint16_t)(((l & 0x3F) << 8) | buf[pos + 1]);
            if (!jumped) used = pos + 2 - start;
            jumped = 1;
            /*
             * Un puntero solo puede apuntar hacia atras. Exigirlo, junto al
             * limite de saltos, hace imposible construir un bucle infinito
             * con un paquete malicioso.
             */
            if (ptr >= pos || ptr >= len) return -1;
            if (++jumps > DNS_MAX_JUMPS) return -1;
            pos = ptr;
            continue;
        }
        if ((l & 0xC0) != 0) return -1;   /* 0x40 y 0x80 estan reservados */

        pos++;
        if (l == 0) {
            if (outlen == 0) {            /* nombre raiz */
                if (outsz < 2) return -1;
                out[outlen++] = '.';
            }
            if (!jumped) used = pos - start;
            break;
        }
        if (l > DNS_MAX_LABEL) return -1;
        if (pos + l > len) return -1;

        {
            size_t i;
            for (i = 0; i < l; i++) {
                unsigned char c = buf[pos + i];

                /* Escapes RFC 1035 para que el nombre pueda re-serializarse. */
                if (c == '.' || c == '\\') {
                    if (outlen + 2 >= outsz) return -1;
                    out[outlen++] = '\\';
                    out[outlen++] = (char)c;
                } else if (c < 0x21 || c > 0x7E) {
                    if (outlen + 4 >= outsz) return -1;
                    out[outlen++] = '\\';
                    out[outlen++] = (char)('0' + (c / 100));
                    out[outlen++] = (char)('0' + ((c / 10) % 10));
                    out[outlen++] = (char)('0' + (c % 10));
                } else {
                    if (outlen + 1 >= outsz) return -1;
                    out[outlen++] = (char)c;
                }
            }
        }
        if (outlen + 1 >= outsz) return -1;
        out[outlen++] = '.';
        pos += l;
    }

    out[outlen] = '\0';
    if (consumed != NULL) *consumed = used;
    return 0;
}

/*
 * Extrae la siguiente etiqueta del nombre en formato presentacion,
 * deshaciendo los escapes. Avanza *pos justo despues del '.' separador.
 */
static int next_label(const char *name, size_t *pos, uint8_t *out, size_t *outlen) {
    size_t n = 0;
    size_t i = *pos;

    while (name[i] != '\0' && name[i] != '.') {
        unsigned char c;

        if (name[i] == '\\') {
            i++;
            if (name[i] == '\0') return -1;
            if (isdigit((unsigned char)name[i])) {
                if (!isdigit((unsigned char)name[i + 1]) ||
                    !isdigit((unsigned char)name[i + 2])) return -1;
                {
                    int v = (name[i] - '0') * 100 +
                            (name[i + 1] - '0') * 10 +
                            (name[i + 2] - '0');
                    if (v > 255) return -1;
                    c = (unsigned char)v;
                }
                i += 3;
            } else {
                c = (unsigned char)name[i];
                i++;
            }
        } else {
            c = (unsigned char)name[i];
            i++;
        }
        if (n >= DNS_MAX_LABEL) return -1;
        out[n++] = c;
    }
    if (name[i] == '.') i++;

    *pos = i;
    *outlen = n;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Writer                                                              */
/* ------------------------------------------------------------------ */

void dns_writer_init(dns_writer_t *w, uint8_t *buf, size_t cap) {
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->overflow = 0;
    w->ctable_used = 0;
}

static int w_bytes(dns_writer_t *w, const void *src, size_t n) {
    if (w->len + n > w->cap) { w->overflow = 1; return -1; }
    memcpy(w->buf + w->len, src, n);
    w->len += n;
    return 0;
}

static int w_u8(dns_writer_t *w, uint8_t v) {
    if (w->len + 1 > w->cap) { w->overflow = 1; return -1; }
    w->buf[w->len++] = v;
    return 0;
}

static int w_u16(dns_writer_t *w, uint16_t v) {
    if (w->len + 2 > w->cap) { w->overflow = 1; return -1; }
    wr16(w->buf + w->len, v);
    w->len += 2;
    return 0;
}

static int w_u32(dns_writer_t *w, uint32_t v) {
    if (w->len + 4 > w->cap) { w->overflow = 1; return -1; }
    wr32(w->buf + w->len, v);
    w->len += 4;
    return 0;
}

/* Busca un sufijo ya escrito para poder apuntarlo con un puntero. */
static int ctable_find(const dns_writer_t *w, const char *name) {
    int i;
    for (i = 0; i < w->ctable_used; i++) {
        if (strcmp(w->ctable[i].name, name) == 0) return w->ctable[i].offset;
    }
    return -1;
}

static void ctable_add(dns_writer_t *w, const char *name, size_t offset) {
    /* Solo caben offsets de 14 bits en un puntero de compresion. */
    if (offset >= 0x4000) return;
    if (w->ctable_used >= DNS_CTABLE_MAX) return;
    if (strlen(name) >= DNS_MAX_NAME) return;

    snprintf(w->ctable[w->ctable_used].name,
             sizeof w->ctable[w->ctable_used].name, "%s", name);
    w->ctable[w->ctable_used].offset = (uint16_t)offset;
    w->ctable_used++;
}

int dns_write_name(dns_writer_t *w, const char *name, int compress) {
    char norm[DNS_MAX_NAME];
    size_t pos = 0;

    dns_name_normalize(name, norm, sizeof norm);

    while (norm[pos] != '\0') {
        uint8_t  label[DNS_MAX_LABEL];
        size_t   llen = 0;

        /* Fin del nombre: solo queda el punto de la raiz. */
        if (norm[pos] == '.' && norm[pos + 1] == '\0') break;

        if (compress) {
            int off = ctable_find(w, norm + pos);
            if (off >= 0) {
                /* 11xxxxxx xxxxxxxx -> puntero al offset ya escrito. */
                return w_u16(w, (uint16_t)(0xC000 | (uint16_t)off));
            }
        }
        ctable_add(w, norm + pos, w->len);

        if (next_label(norm, &pos, label, &llen) < 0) return -1;
        if (llen == 0) return -1;   /* etiqueta vacia en medio del nombre */

        if (w_u8(w, (uint8_t)llen) < 0) return -1;
        if (w_bytes(w, label, llen) < 0) return -1;
    }

    return w_u8(w, 0);
}

int dns_write_header(dns_writer_t *w, const dns_header_t *h) {
    uint16_t flags = 0;

    flags |= (uint16_t)((h->qr     & 0x01) << 15);
    flags |= (uint16_t)((h->opcode & 0x0F) << 11);
    flags |= (uint16_t)((h->aa     & 0x01) << 10);
    flags |= (uint16_t)((h->tc     & 0x01) << 9);
    flags |= (uint16_t)((h->rd     & 0x01) << 8);
    flags |= (uint16_t)((h->ra     & 0x01) << 7);
    flags |= (uint16_t)((h->z      & 0x07) << 4);
    flags |= (uint16_t)(h->rcode   & 0x0F);

    if (w_u16(w, h->id) < 0) return -1;
    if (w_u16(w, flags) < 0) return -1;
    if (w_u16(w, h->qdcount) < 0) return -1;
    if (w_u16(w, h->ancount) < 0) return -1;
    if (w_u16(w, h->nscount) < 0) return -1;
    if (w_u16(w, h->arcount) < 0) return -1;
    return 0;
}

void dns_patch_counts(dns_writer_t *w, uint16_t qd, uint16_t an,
                      uint16_t ns, uint16_t ar) {
    if (w->len < DNS_HEADER_LEN) return;
    wr16(w->buf + 4,  qd);
    wr16(w->buf + 6,  an);
    wr16(w->buf + 8,  ns);
    wr16(w->buf + 10, ar);
}

void dns_patch_tc(dns_writer_t *w, int tc) {
    if (w->len < DNS_HEADER_LEN) return;
    if (tc) w->buf[2] |= 0x02;
    else    w->buf[2] &= (uint8_t)~0x02;
}

int dns_write_question(dns_writer_t *w, const dns_question_t *q) {
    if (dns_write_name(w, q->qname, 1) < 0) return -1;
    if (w_u16(w, q->qtype) < 0) return -1;
    if (w_u16(w, q->qclass) < 0) return -1;
    return 0;
}

/*
 * Serializa el RDATA segun el tipo. Los tipos definidos en el RFC 1035
 * admiten compresion dentro del RDATA; los mas modernos (SRV, SVCB, ...)
 * no, y los opacos se copian tal cual (RFC 3597).
 */
static int write_rdata(dns_writer_t *w, const dns_rr_t *rr) {
    if (!rr->decoded) return w_bytes(w, rr->raw, rr->raw_len);

    switch (rr->type) {
        case DNS_TYPE_A:
            return w_bytes(w, rr->rd.a, 4);

        case DNS_TYPE_AAAA:
            return w_bytes(w, rr->rd.aaaa, 16);

        case DNS_TYPE_NS:
        case DNS_TYPE_CNAME:
        case DNS_TYPE_PTR:
            return dns_write_name(w, rr->rd.name, 1);

        case DNS_TYPE_MX:
            if (w_u16(w, rr->rd.mx.preference) < 0) return -1;
            return dns_write_name(w, rr->rd.mx.exchange, 1);

        case DNS_TYPE_SOA:
            if (dns_write_name(w, rr->rd.soa.mname, 1) < 0) return -1;
            if (dns_write_name(w, rr->rd.soa.rname, 1) < 0) return -1;
            if (w_u32(w, rr->rd.soa.serial)  < 0) return -1;
            if (w_u32(w, rr->rd.soa.refresh) < 0) return -1;
            if (w_u32(w, rr->rd.soa.retry)   < 0) return -1;
            if (w_u32(w, rr->rd.soa.expire)  < 0) return -1;
            return w_u32(w, rr->rd.soa.minimum);

        case DNS_TYPE_SRV:
            if (w_u16(w, rr->rd.srv.priority) < 0) return -1;
            if (w_u16(w, rr->rd.srv.weight)   < 0) return -1;
            if (w_u16(w, rr->rd.srv.port)     < 0) return -1;
            /* RFC 2782: el target de SRV no se comprime. */
            return dns_write_name(w, rr->rd.srv.target, 0);

        default:
            return w_bytes(w, rr->raw, rr->raw_len);
    }
}

int dns_write_rr(dns_writer_t *w, const dns_rr_t *rr) {
    size_t save_len   = w->len;
    int    save_ctab  = w->ctable_used;
    size_t rdlen_at;
    size_t rdata_start;
    size_t rdlen;

    if (dns_write_name(w, rr->name, 1) < 0) goto rollback;
    if (w_u16(w, rr->type)   < 0) goto rollback;
    if (w_u16(w, rr->rclass) < 0) goto rollback;
    if (w_u32(w, rr->ttl)    < 0) goto rollback;

    /* RDLENGTH se rellena despues, cuando sabemos cuanto ocupo el RDATA. */
    rdlen_at = w->len;
    if (w_u16(w, 0) < 0) goto rollback;

    rdata_start = w->len;
    if (write_rdata(w, rr) < 0) goto rollback;

    rdlen = w->len - rdata_start;
    if (rdlen > 0xFFFF) goto rollback;
    wr16(w->buf + rdlen_at, (uint16_t)rdlen);
    return 0;

rollback:
    /* Deshacemos la escritura parcial para que el mensaje siga siendo valido. */
    w->len         = save_len;
    w->ctable_used = save_ctab;
    w->overflow    = 1;
    return -1;
}

int dns_write_opt(dns_writer_t *w, uint16_t payload) {
    size_t save_len  = w->len;
    int    save_ctab = w->ctable_used;

    if (w_u8(w, 0) < 0)                goto rollback;  /* NAME = raiz      */
    if (w_u16(w, DNS_TYPE_OPT) < 0)    goto rollback;  /* TYPE = OPT       */
    if (w_u16(w, payload) < 0)         goto rollback;  /* CLASS = payload  */
    if (w_u32(w, 0) < 0)               goto rollback;  /* TTL  = rcode ext */
    if (w_u16(w, 0) < 0)               goto rollback;  /* RDLENGTH = 0     */
    return 0;

rollback:
    w->len         = save_len;
    w->ctable_used = save_ctab;
    w->overflow    = 1;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Parser                                                              */
/* ------------------------------------------------------------------ */

int dns_parse_header(const uint8_t *buf, size_t len, dns_header_t *h) {
    uint16_t flags;

    if (len < DNS_HEADER_LEN) return DNS_RC_FORMERR;

    h->id     = rd16(buf);
    flags     = rd16(buf + 2);
    h->qr     = (uint8_t)((flags >> 15) & 0x01);
    h->opcode = (uint8_t)((flags >> 11) & 0x0F);
    h->aa     = (uint8_t)((flags >> 10) & 0x01);
    h->tc     = (uint8_t)((flags >> 9)  & 0x01);
    h->rd     = (uint8_t)((flags >> 8)  & 0x01);
    h->ra     = (uint8_t)((flags >> 7)  & 0x01);
    h->z      = (uint8_t)((flags >> 4)  & 0x07);
    h->rcode  = (uint8_t)(flags & 0x0F);

    h->qdcount = rd16(buf + 4);
    h->ancount = rd16(buf + 6);
    h->nscount = rd16(buf + 8);
    h->arcount = rd16(buf + 10);
    return 0;
}

/*
 * Decodifica un RR completo.
 * Retorna 0 si quedo utilizable, 1 si hay que descartarlo (pero *pos ya
 * avanzo correctamente) y -1 si el mensaje esta irrecuperablemente roto.
 */
static int parse_rr(const uint8_t *buf, size_t len, size_t *pos,
                    dns_rr_t *rr, int *is_opt, uint16_t *opt_payload) {
    size_t consumed = 0;
    size_t rdstart;
    uint16_t rdlength;

    memset(rr, 0, sizeof *rr);
    *is_opt = 0;

    if (dns_parse_name(buf, len, *pos, rr->name, sizeof rr->name, &consumed) < 0)
        return -1;
    *pos += consumed;

    if (*pos + 10 > len) return -1;
    rr->type   = rd16(buf + *pos);
    rr->rclass = rd16(buf + *pos + 2);
    rr->ttl    = rd32(buf + *pos + 4);
    rdlength   = rd16(buf + *pos + 8);
    *pos += 10;

    if (*pos + rdlength > len) return -1;
    rdstart = *pos;
    *pos += rdlength;

    if (rr->type == DNS_TYPE_OPT) {
        /* EDNS0: CLASS transporta el tamano de payload UDP del cliente. */
        *is_opt = 1;
        *opt_payload = rr->rclass;
        return 1;
    }

    switch (rr->type) {
        case DNS_TYPE_A:
            if (rdlength != 4) return 1;
            memcpy(rr->rd.a, buf + rdstart, 4);
            rr->decoded = 1;
            break;

        case DNS_TYPE_AAAA:
            if (rdlength != 16) return 1;
            memcpy(rr->rd.aaaa, buf + rdstart, 16);
            rr->decoded = 1;
            break;

        case DNS_TYPE_NS:
        case DNS_TYPE_CNAME:
        case DNS_TYPE_PTR:
            if (dns_parse_name(buf, len, rdstart, rr->rd.name,
                               sizeof rr->rd.name, NULL) < 0) return 1;
            rr->decoded = 1;
            break;

        case DNS_TYPE_MX:
            if (rdlength < 3) return 1;
            rr->rd.mx.preference = rd16(buf + rdstart);
            if (dns_parse_name(buf, len, rdstart + 2, rr->rd.mx.exchange,
                               sizeof rr->rd.mx.exchange, NULL) < 0) return 1;
            rr->decoded = 1;
            break;

        case DNS_TYPE_SOA: {
            size_t p = rdstart, used = 0;

            if (dns_parse_name(buf, len, p, rr->rd.soa.mname,
                               sizeof rr->rd.soa.mname, &used) < 0) return 1;
            p += used;
            if (dns_parse_name(buf, len, p, rr->rd.soa.rname,
                               sizeof rr->rd.soa.rname, &used) < 0) return 1;
            p += used;
            if (p + 20 > len) return 1;
            rr->rd.soa.serial  = rd32(buf + p);
            rr->rd.soa.refresh = rd32(buf + p + 4);
            rr->rd.soa.retry   = rd32(buf + p + 8);
            rr->rd.soa.expire  = rd32(buf + p + 12);
            rr->rd.soa.minimum = rd32(buf + p + 16);
            rr->decoded = 1;
            break;
        }

        case DNS_TYPE_SRV:
            if (rdlength < 7) return 1;
            rr->rd.srv.priority = rd16(buf + rdstart);
            rr->rd.srv.weight   = rd16(buf + rdstart + 2);
            rr->rd.srv.port     = rd16(buf + rdstart + 4);
            if (dns_parse_name(buf, len, rdstart + 6, rr->rd.srv.target,
                               sizeof rr->rd.srv.target, NULL) < 0) return 1;
            rr->decoded = 1;
            break;

        default:
            /*
             * TXT y todos los tipos que no decodificamos (SVCB, HTTPS, CAA,
             * DNSKEY, ...). El RFC 3597 prohibe comprimir nombres dentro del
             * RDATA de tipos desconocidos, asi que la copia cruda es exacta.
             */
            if (rdlength > DNS_MAX_RDATA) return 1;
            memcpy(rr->raw, buf + rdstart, rdlength);
            rr->raw_len = rdlength;
            rr->decoded = 0;
            break;
    }

    return 0;
}

static int parse_section(const uint8_t *buf, size_t len, size_t *pos,
                         uint16_t count, dns_rr_t *out, uint16_t *used,
                         uint8_t *has_opt, uint16_t *opt_payload) {
    uint16_t i;

    for (i = 0; i < count; i++) {
        dns_rr_t  rr;
        int       is_opt = 0;
        uint16_t  payload = 0;
        int       rc = parse_rr(buf, len, pos, &rr, &is_opt, &payload);

        if (rc < 0) return -1;
        if (is_opt) {
            if (has_opt != NULL) {
                *has_opt = 1;
                *opt_payload = payload;
            }
            continue;
        }
        if (rc > 0) continue;                 /* RR ilegible: se descarta   */
        if (*used >= DNS_MAX_RR) continue;    /* seccion llena: se ignora   */

        out[*used] = rr;
        (*used)++;
    }
    return 0;
}

int dns_parse_message(const uint8_t *buf, size_t len, dns_message_t *msg) {
    size_t pos = DNS_HEADER_LEN;
    uint16_t i;
    int rc;

    memset(msg, 0, sizeof *msg);

    rc = dns_parse_header(buf, len, &msg->header);
    if (rc != 0) return rc;

    /* Seccion Question: guardamos la primera y saltamos el resto. */
    for (i = 0; i < msg->header.qdcount; i++) {
        char     qname[DNS_MAX_NAME];
        size_t   consumed = 0;
        uint16_t qtype, qclass;

        if (dns_parse_name(buf, len, pos, qname, sizeof qname, &consumed) < 0)
            return DNS_RC_FORMERR;
        pos += consumed;
        if (pos + 4 > len) return DNS_RC_FORMERR;
        qtype  = rd16(buf + pos);
        qclass = rd16(buf + pos + 2);
        pos += 4;

        if (i == 0) {
            snprintf(msg->question.qname, sizeof msg->question.qname, "%s", qname);
            msg->question.qtype  = qtype;
            msg->question.qclass = qclass;
            msg->has_question = 1;
        }
    }

    if (parse_section(buf, len, &pos, msg->header.ancount,
                      msg->answer, &msg->an_used, NULL, NULL) < 0)
        return DNS_RC_FORMERR;
    if (parse_section(buf, len, &pos, msg->header.nscount,
                      msg->authority, &msg->ns_used, NULL, NULL) < 0)
        return DNS_RC_FORMERR;
    if (parse_section(buf, len, &pos, msg->header.arcount,
                      msg->additional, &msg->ar_used,
                      &msg->has_opt, &msg->opt_payload) < 0)
        return DNS_RC_FORMERR;

    return 0;
}

/* ------------------------------------------------------------------ */
/* Conversion texto <-> RDATA                                          */
/* ------------------------------------------------------------------ */

int dns_rr_set_from_text(dns_rr_t *rr, uint16_t type, const char *value) {
    rr->type    = type;
    rr->decoded = 1;
    rr->raw_len = 0;

    switch (type) {
        case DNS_TYPE_A:
            if (inet_pton(AF_INET, value, rr->rd.a) != 1) return -1;
            return 0;

        case DNS_TYPE_AAAA:
            if (inet_pton(AF_INET6, value, rr->rd.aaaa) != 1) return -1;
            return 0;

        case DNS_TYPE_NS:
        case DNS_TYPE_CNAME:
        case DNS_TYPE_PTR:
            dns_name_normalize(value, rr->rd.name, sizeof rr->rd.name);
            return 0;

        case DNS_TYPE_MX: {
            unsigned pref;
            char host[DNS_MAX_NAME];

            if (sscanf(value, "%u %255s", &pref, host) != 2) return -1;
            rr->rd.mx.preference = (uint16_t)pref;
            dns_name_normalize(host, rr->rd.mx.exchange, sizeof rr->rd.mx.exchange);
            return 0;
        }

        case DNS_TYPE_SRV: {
            unsigned prio, weight, port;
            char target[DNS_MAX_NAME];

            if (sscanf(value, "%u %u %u %255s", &prio, &weight, &port, target) != 4)
                return -1;
            rr->rd.srv.priority = (uint16_t)prio;
            rr->rd.srv.weight   = (uint16_t)weight;
            rr->rd.srv.port     = (uint16_t)port;
            dns_name_normalize(target, rr->rd.srv.target, sizeof rr->rd.srv.target);
            return 0;
        }

        case DNS_TYPE_SOA: {
            char mname[DNS_MAX_NAME], rname[DNS_MAX_NAME];
            unsigned long serial, refresh, retry, expire, minimum;

            if (sscanf(value, "%255s %255s %lu %lu %lu %lu %lu",
                       mname, rname, &serial, &refresh, &retry,
                       &expire, &minimum) != 7) return -1;
            dns_name_normalize(mname, rr->rd.soa.mname, sizeof rr->rd.soa.mname);
            dns_name_normalize(rname, rr->rd.soa.rname, sizeof rr->rd.soa.rname);
            rr->rd.soa.serial  = (uint32_t)serial;
            rr->rd.soa.refresh = (uint32_t)refresh;
            rr->rd.soa.retry   = (uint32_t)retry;
            rr->rd.soa.expire  = (uint32_t)expire;
            rr->rd.soa.minimum = (uint32_t)minimum;
            return 0;
        }

        case DNS_TYPE_TXT: {
            /*
             * El RDATA de TXT es una secuencia de <character-string>:
             * cada trozo lleva su longitud en un byte y no puede pasar de 255.
             */
            size_t vlen = strlen(value);
            size_t off = 0;

            if (vlen >= 2 && value[0] == '"' && value[vlen - 1] == '"') {
                value++;
                vlen -= 2;
            }
            rr->decoded = 0;
            while (off < vlen) {
                size_t chunk = vlen - off;

                if (chunk > 255) chunk = 255;
                if (rr->raw_len + 1 + chunk > DNS_MAX_RDATA) return -1;
                rr->raw[rr->raw_len++] = (uint8_t)chunk;
                memcpy(rr->raw + rr->raw_len, value + off, chunk);
                rr->raw_len = (uint16_t)(rr->raw_len + chunk);
                off += chunk;
            }
            if (rr->raw_len == 0) rr->raw[rr->raw_len++] = 0;  /* TXT vacio */
            return 0;
        }

        default:
            return -1;
    }
}

void dns_rr_to_text(const dns_rr_t *rr, char *out, size_t outsz) {
    char tmp[INET6_ADDRSTRLEN];

    if (!rr->decoded) {
        if (rr->type == DNS_TYPE_TXT) {
            /* Concatena los character-string para mostrarlos legibles. */
            size_t i = 0, n = 0;

            while (i < rr->raw_len && n + 1 < outsz) {
                size_t l = rr->raw[i++];

                if (i + l > rr->raw_len) break;
                while (l-- > 0 && n + 1 < outsz) {
                    unsigned char c = rr->raw[i++];
                    out[n++] = (c >= 0x20 && c < 0x7F) ? (char)c : '.';
                }
            }
            out[n] = '\0';
            return;
        }
        snprintf(out, outsz, "<%u bytes>", rr->raw_len);
        return;
    }

    switch (rr->type) {
        case DNS_TYPE_A:
            inet_ntop(AF_INET, rr->rd.a, tmp, sizeof tmp);
            snprintf(out, outsz, "%s", tmp);
            break;
        case DNS_TYPE_AAAA:
            inet_ntop(AF_INET6, rr->rd.aaaa, tmp, sizeof tmp);
            snprintf(out, outsz, "%s", tmp);
            break;
        case DNS_TYPE_NS:
        case DNS_TYPE_CNAME:
        case DNS_TYPE_PTR:
            snprintf(out, outsz, "%s", rr->rd.name);
            break;
        case DNS_TYPE_MX:
            snprintf(out, outsz, "%u %s", rr->rd.mx.preference, rr->rd.mx.exchange);
            break;
        case DNS_TYPE_SRV:
            snprintf(out, outsz, "%u %u %u %s", rr->rd.srv.priority,
                     rr->rd.srv.weight, rr->rd.srv.port, rr->rd.srv.target);
            break;
        case DNS_TYPE_SOA:
            snprintf(out, outsz, "%s %s %u %u %u %u %u",
                     rr->rd.soa.mname, rr->rd.soa.rname, rr->rd.soa.serial,
                     rr->rd.soa.refresh, rr->rd.soa.retry, rr->rd.soa.expire,
                     rr->rd.soa.minimum);
            break;
        default:
            snprintf(out, outsz, "<%u bytes>", rr->raw_len);
            break;
    }
}
