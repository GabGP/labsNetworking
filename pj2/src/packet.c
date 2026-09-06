#include "packet.h"

#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>

/* Pseudo-header UDP: solo se usa para el checksum, no viaja en la red */
#pragma pack(push, 1)
typedef struct {
    uint32_t src_addr;
    uint32_t dst_addr;
    uint8_t  zero;
    uint8_t  protocol;
    uint16_t udp_length;
} pseudo_header_t;
#pragma pack(pop)

uint16_t calculate_checksum(const void *buffer, int size)
{
    const uint8_t *p = (const uint8_t *)buffer;
    unsigned long sum = 0;
    uint16_t word;

    while (size > 1) {
        memcpy(&word, p, sizeof(word));
        sum += word;
        p += 2;
        size -= 2;
    }
    if (size == 1) {
        sum += *p;              /* ultimo byte impar, si lo hay */
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

uint16_t calculate_udp_checksum(uint32_t src_addr, uint32_t dst_addr,
                                const void *udp_segment, int udp_segment_len)
{
    uint8_t pseudo_packet[sizeof(pseudo_header_t) + PROBE_LEN];
    pseudo_header_t psh;
    int total_len;

    if (udp_segment_len < 0 || udp_segment_len > PROBE_LEN) {
        return 0;
    }
    total_len = (int)sizeof(pseudo_header_t) + udp_segment_len;

    psh.src_addr   = src_addr;
    psh.dst_addr   = dst_addr;
    psh.zero       = 0;
    psh.protocol   = IPPROTO_UDP;
    psh.udp_length = htons((uint16_t)udp_segment_len);

    memcpy(pseudo_packet, &psh, sizeof(psh));
    memcpy(pseudo_packet + sizeof(psh), udp_segment, (size_t)udp_segment_len);

    return calculate_checksum(pseudo_packet, total_len);
}

void build_probe_packet(uint8_t *buf, uint32_t src_addr, uint32_t dst_addr,
                        uint8_t ttl, uint16_t src_port, uint16_t dst_port,
                        uint16_t ip_id)
{
    ip_header_t iph;
    udp_header_t udph;
    const int udp_len = PROBE_LEN - IP_HEADER_LEN;   /* 8 header + 32 relleno */

    memset(buf, 0, PROBE_LEN);

    /* --- Header IP (RFC 791) --- */
    memset(&iph, 0, sizeof(iph));
    iph.ihl_version  = (4 << 4) | 5;                 /* version 4, IHL 5 -> 20 bytes */
    iph.tos          = 0;
    iph.total_length = htons((uint16_t)PROBE_LEN);
    iph.id           = htons(ip_id);
    iph.flags_fo     = 0;                            /* sin fragmentar, offset 0 */
    iph.ttl          = ttl;                          /* TTL del salto que se sondea */
    iph.protocol     = IPPROTO_UDP;                  /* 17 */
    iph.checksum     = 0;                            /* en cero para calcularlo */
    iph.src_addr     = src_addr;
    iph.dst_addr     = dst_addr;
    iph.checksum     = calculate_checksum(&iph, IP_HEADER_LEN);
    memcpy(buf, &iph, sizeof(iph));

    /* --- Header UDP (RFC 768) --- */
    memset(&udph, 0, sizeof(udph));
    udph.src_port = htons(src_port);
    udph.dst_port = htons(dst_port);
    udph.length   = htons((uint16_t)udp_len);
    udph.checksum = 0;
    memcpy(buf + IP_HEADER_LEN, &udph, sizeof(udph));

    /* El checksum UDP cubre el segmento completo (header + relleno en cero),
       por eso se calcula ya con el segmento copiado dentro de buf. */
    udph.checksum = calculate_udp_checksum(src_addr, dst_addr,
                                           buf + IP_HEADER_LEN, udp_len);
    memcpy(buf + IP_HEADER_LEN, &udph, sizeof(udph));
}

int parse_icmp_reply(const uint8_t *packet, int len, uint16_t src_port,
                     icmp_reply_t *reply)
{
    ip_header_t inner_iph;
    udp_header_t inner_udph;
    int outer_ihl, icmp_off, inner_off, inner_ihl, udp_off;

    /* 1. Header IP externo: el que trae el router que respondio */
    if (len < IP_HEADER_LEN) {
        return 0;
    }
    outer_ihl = (packet[0] & 0x0F) * 4;
    if (outer_ihl < IP_HEADER_LEN || len < outer_ihl + ICMP_HEADER_LEN) {
        return 0;
    }

    /* 2. Header ICMP: solo interesan Time Exceeded y Destination Unreachable,
          que son los unicos que citan el datagrama original */
    icmp_off = outer_ihl;
    reply->type = packet[icmp_off];
    reply->code = packet[icmp_off + 1];
    if (reply->type != ICMP_TIME_EXCEEDED && reply->type != ICMP_DEST_UNREACH) {
        return 0;
    }

    /* 3. Header IP citado dentro del ICMP: debe ser UDP para ser nuestro */
    inner_off = icmp_off + ICMP_HEADER_LEN;
    if (len < inner_off + IP_HEADER_LEN) {
        return 0;
    }
    memcpy(&inner_iph, packet + inner_off, sizeof(inner_iph));
    inner_ihl = (inner_iph.ihl_version & 0x0F) * 4;
    if (inner_ihl < IP_HEADER_LEN || inner_iph.protocol != IPPROTO_UDP) {
        return 0;
    }

    /* 4. Primeros 8 bytes del UDP citado: aqui viven los puertos que
          identifican de que sondeo se trata (RFC 792 garantiza estos 8 bytes) */
    udp_off = inner_off + inner_ihl;
    if (len < udp_off + UDP_HEADER_LEN) {
        return 0;
    }
    memcpy(&inner_udph, packet + udp_off, sizeof(inner_udph));
    if (ntohs(inner_udph.src_port) != src_port) {
        return 0;                       /* trafico ajeno: ping, otro proceso */
    }

    reply->probe_port = ntohs(inner_udph.dst_port);
    return 1;
}
