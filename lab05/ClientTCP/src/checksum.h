#ifndef CHECKSUM_H
#define CHECKSUM_H

#include <stdint.h>
#include <stddef.h>

/*
 * Checksum genérico "Internet checksum" (RFC 1071).
 * Lo usan tanto el header IP como, junto al pseudo-header, el header TCP.
 * 'buffer' debe apuntar a datos de tamaño 'size' en bytes.
 */
uint16_t calculate_checksum(uint16_t *buffer, int size);

/*
 * Calcula el checksum de un segmento TCP incluyendo el pseudo-header
 * (IP origen, IP destino, protocolo y longitud), tal como exige RFC 793.
 * tcp_segment debe apuntar al header TCP seguido de los datos,
 * y tcp_segment_len es su longitud total (header + datos).
 * src_addr / dst_addr deben venir ya en orden de red (network byte order),
 * es decir, tal como quedan en ip_header_t.src_addr / dst_addr.
 */
uint16_t calculate_tcp_checksum(uint32_t src_addr, uint32_t dst_addr,
                                 uint8_t *tcp_segment, int tcp_segment_len);

#endif /* CHECKSUM_H */
