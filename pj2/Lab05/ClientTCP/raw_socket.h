#ifndef RAW_SOCKET_H
#define RAW_SOCKET_H

#include <stdint.h>
#include <stddef.h>

/*
 * Crea el socket de ENVÍO: un raw socket TCP con IP_HDRINCL, para armar
 * nosotros mismos el header IP.
 *
 * En macOS/BSD, ADEMÁS abre una captura vía libpcap sobre 'capture_interface'
 * (por ejemplo "lo0" para pruebas locales, o "en0" en una red real), porque
 * en BSD un raw socket con IPPROTO_TCP NO recibe paquetes entrantes: el
 * kernel los consume por completo con su propio stack TCP. Sólo entrega a
 * raw sockets los protocolos que él mismo no maneja (como ICMP). Por eso en
 * macOS hay que "espiar" la interfaz con pcap, igual que hace tcpdump o
 * Wireshark, para poder ver el SYN+ACK del servidor.
 * En Linux 'capture_interface' se ignora (se puede pasar NULL).
 *
 * Requiere privilegios de root.
 * Retorna el file descriptor del socket de envío, o -1 en caso de error.
 */
int create_raw_socket(const char *capture_interface);

/*
 * Envía 'packet' (IP header + TCP header + datos, ya armado y con
 * checksums calculados) de tamaño packet_len hacia dst_ip.
 * Retorna la cantidad de bytes enviados, o -1 en caso de error.
 */
int send_packet(int sockfd, uint8_t *packet, size_t packet_len, const char *dst_ip);

/*
 * Recibe el próximo paquete IP (header IP incluido, SIN headers de enlace)
 * en buffer, esperando como máximo timeout_ms milisegundos.
 *
 * En Linux se implementa con recvfrom() sobre el raw socket.
 * En macOS se implementa leyendo de la captura pcap abierta por
 * create_raw_socket() (el parámetro sockfd se ignora en ese caso).
 *
 * Retorna la cantidad de bytes recibidos, o -1 si no llegó nada a tiempo.
 */
int receive_packet(int sockfd, uint8_t *buffer, size_t buffer_len, int timeout_ms);

/*
 * Libera los recursos usados para recibir/enviar (cierra el socket y,
 * en macOS, también la captura pcap).
 */
void close_raw_socket(int sockfd);

#endif /* RAW_SOCKET_H */