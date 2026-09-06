#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>

/*
 * Estructuras y utilidades para armar a mano el datagrama de sondeo
 * (IPv4 + UDP) y para interpretar las respuestas ICMP.
 * Se mantiene el estilo de ip_header.h / checksum.h del Lab #05:
 * structs empaquetadas con #pragma pack y checksum propio (RFC 1071).
 */

#pragma pack(push, 1)
typedef struct {
    uint8_t  ihl_version;   /* 4 bits version + 4 bits IHL (palabras de 32 bits) */
    uint8_t  tos;           /* Type of Service */
    uint16_t total_length;  /* header IP + datos, en orden de red */
    uint16_t id;            /* identificador del paquete */
    uint16_t flags_fo;      /* 3 bits flags + 13 bits fragment offset */
    uint8_t  ttl;           /* Time To Live: el corazon de traceroute */
    uint8_t  protocol;      /* protocolo encapsulado (17 = UDP) */
    uint16_t checksum;      /* checksum del header IP */
    uint32_t src_addr;      /* IP origen, en orden de red */
    uint32_t dst_addr;      /* IP destino, en orden de red */
} ip_header_t;

typedef struct {
    uint16_t src_port;      /* puerto origen fijo durante toda la corrida */
    uint16_t dst_port;      /* 33434 + numero de sondeo: identifica el probe */
    uint16_t length;        /* header UDP + datos */
    uint16_t checksum;      /* checksum sobre pseudo-header + segmento */
} udp_header_t;

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint32_t rest;          /* sin uso en type 11 y type 3 */
} icmp_header_t;
#pragma pack(pop)

#define IP_HEADER_LEN   ((int)sizeof(ip_header_t))    /* 20 */
#define UDP_HEADER_LEN  ((int)sizeof(udp_header_t))   /*  8 */
#define ICMP_HEADER_LEN ((int)sizeof(icmp_header_t))  /*  8 */

/* Tamano total del datagrama de sondeo: 20 IP + 8 UDP + 32 de relleno */
#define PROBE_LEN 60

#define ICMP_TIME_EXCEEDED      11
#define ICMP_DEST_UNREACH        3
#define ICMP_PORT_UNREACH_CODE   3

/*
 * Checksum "Internet checksum" (RFC 1071), el mismo del Lab #05.
 * Recibe void* y lee de dos en dos bytes con memcpy para no depender
 * del alineamiento del buffer.
 */
uint16_t calculate_checksum(const void *buffer, int size);

/*
 * Checksum UDP: se calcula sobre el pseudo-header (IP origen, IP destino,
 * cero, protocolo, longitud UDP) seguido del segmento UDP completo.
 * src_addr / dst_addr vienen ya en orden de red.
 */
uint16_t calculate_udp_checksum(uint32_t src_addr, uint32_t dst_addr,
                                const void *udp_segment, int udp_segment_len);

/*
 * Escribe en buf (PROBE_LEN bytes) el datagrama completo: header IP con el
 * TTL del salto actual, header UDP con el puerto destino del sondeo y
 * relleno en cero. Ambos checksums quedan ya calculados.
 */
void build_probe_packet(uint8_t *buf, uint32_t src_addr, uint32_t dst_addr,
                        uint8_t ttl, uint16_t src_port, uint16_t dst_port,
                        uint16_t ip_id);

/* Datos que interesan de una respuesta ICMP ya validada */
typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t probe_port;    /* puerto destino citado: identifica el sondeo */
} icmp_reply_t;

/*
 * Interpreta un paquete recibido en el socket raw ICMP: salta el header IP
 * externo, lee type/code y valida el header IP + los primeros 8 bytes del
 * header UDP que el router cita dentro del mensaje ICMP.
 * Devuelve 1 solo si el paquete es una respuesta a un sondeo nuestro
 * (protocolo interno UDP y puerto origen interno == src_port); 0 si no.
 */
int parse_icmp_reply(const uint8_t *packet, int len, uint16_t src_port,
                     icmp_reply_t *reply);

#endif /* PACKET_H */
