#ifndef TCP_HEADER_H
#define TCP_HEADER_H

#include <stdint.h>

/*
 * Estructura del header TCP (RFC 793), 20 bytes sin opciones.
 */
#pragma pack(push, 1)
typedef struct {
    uint16_t src_port;              /* Puerto origen */
    uint16_t dst_port;              /* Puerto destino */
    uint32_t seq_num;               /* Número de secuencia */
    uint32_t ack_num;               /* Número de acknowledgment */
    uint8_t  data_offset_reserved;  /* 4 bits data offset + 4 bits reservados */
    uint8_t  flags;                  /* URG ACK PSH RST SYN FIN */
    uint16_t window;                /* Ventana de recepción */
    uint16_t checksum;              /* Checksum del segmento TCP */
    uint16_t urgent_ptr;            /* Puntero urgente */
} tcp_header_t;
#pragma pack(pop)

#define TCP_HEADER_LEN sizeof(tcp_header_t)

/* Banderas TCP */
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10
#define TCP_URG 0x20

/*
 * Llena tcph con los valores de un header TCP estándar (sin opciones).
 * El checksum se deja en 0; se calcula aparte con checksum.c, ya que
 * necesita el pseudo-header (IPs origen/destino).
 */
void build_tcp_header(tcp_header_t *tcph, uint16_t src_port, uint16_t dst_port,
                       uint32_t seq_num, uint32_t ack_num, uint8_t flags,
                       uint16_t window);

#endif /* TCP_HEADER_H */
