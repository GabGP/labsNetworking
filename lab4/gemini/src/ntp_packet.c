#define _POSIX_C_SOURCE 200809L
#include "ntp_packet.h"
#include <string.h>
#include <arpa/inet.h>

void ntp_packet_init_request(uint8_t *buffer, size_t buf_size, ntp_time_t t1) {
    if (!buffer || buf_size < NTP_PACKET_SIZE) return;

    memset(buffer, 0, NTP_PACKET_SIZE);

    /* LI = 0 (0b00), VN = 3 (0b011), Mode = 3 (0b011 client) -> 0x1B */
    buffer[0] = (0 << 6) | (3 << 3) | 3;
    buffer[1] = 0; /* Stratum 0 */
    buffer[2] = 4; /* Poll interval */
    buffer[3] = (uint8_t)(-6); /* Precision */

    /* Write T1 (Transmit timestamp field in request, bytes 40-47) */
    uint32_t sec_net  = htonl(t1.seconds);
    uint32_t frac_net = htonl(t1.fraction);

    memcpy(&buffer[40], &sec_net, sizeof(sec_net));
    memcpy(&buffer[44], &frac_net, sizeof(frac_net));
}

bool ntp_packet_parse_response(const uint8_t *buffer, size_t len, ntp_packet_t *pkt) {
    if (!buffer || len < NTP_PACKET_SIZE || !pkt) return false;

    memset(pkt, 0, sizeof(*pkt));

    pkt->li        = (buffer[0] >> 6) & 0x03;
    pkt->version   = (buffer[0] >> 3) & 0x07;
    pkt->mode      = buffer[0] & 0x07;
    pkt->stratum   = buffer[1];
    pkt->poll      = buffer[2];
    pkt->precision = (int8_t)buffer[3];

    uint32_t root_delay, root_disp, ref_id;
    memcpy(&root_delay, &buffer[4], 4);
    memcpy(&root_disp,  &buffer[8], 4);
    memcpy(&ref_id,     &buffer[12], 4);

    pkt->root_delay      = ntohl(root_delay);
    pkt->root_dispersion = ntohl(root_disp);
    pkt->ref_id          = ntohl(ref_id);

    uint32_t sec, frac;

    /* Reference Timestamp (bytes 16-23) */
    memcpy(&sec,  &buffer[16], 4);
    memcpy(&frac, &buffer[20], 4);
    pkt->ref_ts.seconds  = ntohl(sec);
    pkt->ref_ts.fraction = ntohl(frac);

    /* Originate Timestamp T1 (bytes 24-31) */
    memcpy(&sec,  &buffer[24], 4);
    memcpy(&frac, &buffer[28], 4);
    pkt->orig_ts.seconds  = ntohl(sec);
    pkt->orig_ts.fraction = ntohl(frac);

    /* Receive Timestamp T2 (bytes 32-39) */
    memcpy(&sec,  &buffer[32], 4);
    memcpy(&frac, &buffer[36], 4);
    pkt->recv_ts.seconds  = ntohl(sec);
    pkt->recv_ts.fraction = ntohl(frac);

    /* Transmit Timestamp T3 (bytes 40-47) */
    memcpy(&sec,  &buffer[40], 4);
    memcpy(&frac, &buffer[44], 4);
    pkt->tx_ts.seconds  = ntohl(sec);
    pkt->tx_ts.fraction = ntohl(frac);

    return true;
}
