/*
 * dns_proto.h - Constantes y estructuras del protocolo DNS (RFC 1035 / RFC 5395).
 *
 * Laboratorio 3 - Ciencias de la Computacion VIII
 * Servidor DNS sobre UDP implementado en C puro.
 */
#ifndef DNS_PROTO_H
#define DNS_PROTO_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Limites del protocolo                                               */
/* ------------------------------------------------------------------ */

#define DNS_HEADER_LEN      12      /* Cabecera fija de 12 bytes            */
#define DNS_MAX_UDP         512     /* Tamano clasico sin EDNS0 (RFC 1035)  */
#define DNS_MAX_MSG         65535   /* Limite absoluto de un mensaje DNS    */
#define DNS_MAX_NAME        256     /* Nombre presentacion "a.b.c." + NUL   */
#define DNS_MAX_LABEL       63      /* Longitud maxima de una etiqueta      */
#define DNS_MAX_JUMPS       32      /* Anti-bucle en punteros de compresion */
#define DNS_MAX_RR          32      /* RRs que guardamos por seccion        */
#define DNS_MAX_RDATA       1024    /* RDATA crudo para tipos no decodifica */

/* ------------------------------------------------------------------ */
/* QTYPE - Tab "LISTA TIPO REGISTROS (QTYPE)" del Laboratorio DNS.xlsx  */
/* ------------------------------------------------------------------ */

#define DNS_TYPE_A          1       /* 0x0001 Direccion IPv4                */
#define DNS_TYPE_NS         2       /* 0x0002 Name Server                   */
#define DNS_TYPE_CNAME      5       /* 0x0005 Canonical Name                */
#define DNS_TYPE_SOA        6       /* 0x0006 Start Of Authority            */
#define DNS_TYPE_PTR        12      /* 0x000C Puntero (DNS reverso)         */
#define DNS_TYPE_MX         15      /* 0x000F Mail Exchange                 */
#define DNS_TYPE_TXT        16      /* 0x0010 Texto                         */
#define DNS_TYPE_AAAA       28      /* 0x001C Direccion IPv6                */
#define DNS_TYPE_SRV        33      /* 0x0021 Localizador de servicios      */
#define DNS_TYPE_OPT        41      /* 0x0029 Pseudo-RR EDNS0 (RFC 6891)    */
#define DNS_TYPE_SVCB       64      /* 0x0040 Service Binding               */
#define DNS_TYPE_HTTPS      65      /* 0x0041 HTTPS Binding                 */
#define DNS_TYPE_CAA        257     /* 0x0101 Certification Authority Auth. */
#define DNS_TYPE_ANY        255     /* 0x00FF Consulta comodin              */

/* QCLASS */
#define DNS_CLASS_IN        1       /* Internet (el caso normal)            */
#define DNS_CLASS_CS        2
#define DNS_CLASS_CH        3
#define DNS_CLASS_HS        4
#define DNS_CLASS_ANY       255

/* OPCODE - RFC 5395 seccion 2.2 */
#define DNS_OP_QUERY        0
#define DNS_OP_IQUERY       1
#define DNS_OP_STATUS       2
#define DNS_OP_NOTIFY       4
#define DNS_OP_UPDATE       5

/* RCODE - RFC 5395 seccion 2.3 */
#define DNS_RC_NOERROR      0
#define DNS_RC_FORMERR      1
#define DNS_RC_SERVFAIL     2
#define DNS_RC_NXDOMAIN     3
#define DNS_RC_NOTIMP       4
#define DNS_RC_REFUSED      5

/* ------------------------------------------------------------------ */
/* Cabecera DNS - Tab "HEADER" del Laboratorio DNS.xlsx                 */
/*                                                                      */
/*   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15                     */
/* +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+                    */
/* |                      ID                       |                    */
/* +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+                    */
/* |QR|   Opcode  |AA|TC|RD|RA|   Z    |   RCODE   |                    */
/* +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+                    */
/* |                    QDCOUNT                    |                    */
/* |                    ANCOUNT                    |                    */
/* |                    NSCOUNT                    |                    */
/* |                    ARCOUNT                    |                    */
/* +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+                    */
/* ------------------------------------------------------------------ */

typedef struct {
    uint16_t id;
    uint8_t  qr;        /* 0 = query, 1 = response                      */
    uint8_t  opcode;    /* 4 bits                                       */
    uint8_t  aa;        /* Authoritative Answer                         */
    uint8_t  tc;        /* TrunCation                                   */
    uint8_t  rd;        /* Recursion Desired                            */
    uint8_t  ra;        /* Recursion Available                          */
    uint8_t  z;         /* 3 bits reservados (Z, AD, CD)                */
    uint8_t  rcode;     /* 4 bits                                       */
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

/* Seccion Question: QNAME + QTYPE + QCLASS */
typedef struct {
    char     qname[DNS_MAX_NAME];   /* formato presentacion: "dns.google." */
    uint16_t qtype;
    uint16_t qclass;
} dns_question_t;

/*
 * Un Resource Record ya decodificado.
 *
 * Los tipos que sabemos re-serializar guardan su RDATA en la union `rd`
 * (decoded = 1). Cualquier otro tipo conserva el RDATA crudo en `raw`
 * (decoded = 0); segun RFC 3597 los tipos desconocidos no pueden usar
 * compresion dentro de su RDATA, por lo que copiarlos byte a byte es
 * seguro.
 */
typedef struct {
    char     name[DNS_MAX_NAME];
    uint16_t type;
    uint16_t rclass;
    uint32_t ttl;
    uint8_t  decoded;               /* 1 = union rd valida, 0 = usar raw   */

    union {
        uint8_t  a[4];                      /* A                           */
        uint8_t  aaaa[16];                  /* AAAA                        */
        char     name[DNS_MAX_NAME];        /* NS, CNAME, PTR              */
        struct {
            uint16_t preference;
            char     exchange[DNS_MAX_NAME];
        } mx;                               /* MX                          */
        struct {
            char     mname[DNS_MAX_NAME];
            char     rname[DNS_MAX_NAME];
            uint32_t serial;
            uint32_t refresh;
            uint32_t retry;
            uint32_t expire;
            uint32_t minimum;
        } soa;                              /* SOA                         */
        struct {
            uint16_t priority;
            uint16_t weight;
            uint16_t port;
            char     target[DNS_MAX_NAME];
        } srv;                              /* SRV                         */
    } rd;

    uint8_t  raw[DNS_MAX_RDATA];    /* RDATA crudo (TXT y tipos opacos)    */
    uint16_t raw_len;
} dns_rr_t;

/* Mensaje DNS completo ya decodificado. */
typedef struct {
    dns_header_t   header;
    dns_question_t question;        /* solo soportamos QDCOUNT <= 1        */
    uint8_t        has_question;

    dns_rr_t  answer[DNS_MAX_RR];
    uint16_t  an_used;
    dns_rr_t  authority[DNS_MAX_RR];
    uint16_t  ns_used;
    dns_rr_t  additional[DNS_MAX_RR];
    uint16_t  ar_used;

    /* EDNS0: se extrae del pseudo-RR OPT y no se cuenta en `additional`. */
    uint8_t   has_opt;
    uint16_t  opt_payload;          /* tamano UDP anunciado por el cliente */
} dns_message_t;

/* Nombres legibles para los logs. */
const char *dns_type_name(uint16_t type);
const char *dns_class_name(uint16_t rclass);
const char *dns_rcode_name(uint8_t rcode);
const char *dns_opcode_name(uint8_t opcode);

/* Traduce un mnemonico ("A", "AAAA", ...) a su QTYPE; 0 si no existe. */
uint16_t dns_type_from_name(const char *name);

#endif /* DNS_PROTO_H */
