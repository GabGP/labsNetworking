#include "ip_header.h"

#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>

/* Identificador incremental simple para el campo "id" del header IP */
static uint16_t ip_id_counter = 54321;

void build_ip_header(ip_header_t *iph, const char *src_ip, const char *dst_ip,
                      uint16_t payload_len) {
    memset(iph, 0, sizeof(ip_header_t));

    iph->ihl_version   = (4 << 4) | 5;      /* Version 4, IHL 5 -> 5*4 = 20 bytes */
    iph->tos            = 0;

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    /*
     * Particularidad histórica de BSD (heredada por macOS/Darwin): cuando se
     * usa IP_HDRINCL, el stack de red espera ip_len (total_length) e ip_off
     * (flags_fo) en BYTE ORDER DEL HOST, no en network byte order. El resto
     * del header sí va en network byte order, como es normal.
     * Si se les aplica htons() en macOS, sendto() falla con EINVAL
     * ("Invalid argument"), que es justo el síntoma que da este error.
     */
    iph->total_length = (uint16_t)(sizeof(ip_header_t) + payload_len);
    iph->flags_fo      = 0x4000; /* Flag "Don't Fragment" activado */
#else
    iph->total_length = htons((uint16_t)(sizeof(ip_header_t) + payload_len));
    iph->flags_fo      = htons(0x4000); /* Flag "Don't Fragment" activado */
#endif

    iph->id             = htons(ip_id_counter++);
    iph->ttl            = 64;
    iph->protocol       = IPPROTO_TCP;       /* 6 */
    iph->checksum       = 0;                  /* Se calcula después, con checksum.c */
    iph->src_addr       = inet_addr(src_ip);
    iph->dst_addr       = inet_addr(dst_ip);
}