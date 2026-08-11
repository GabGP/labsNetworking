#ifndef NTP_PACKET_H
#define NTP_PACKET_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "ntp_time.h"

#define NTP_PACKET_SIZE 48

typedef struct {
    uint8_t  li;              /* Leap Indicator (2 bits) */
    uint8_t  version;         /* Version Number (3 bits) */
    uint8_t  mode;            /* Mode (3 bits) */
    uint8_t  stratum;         /* Stratum level (8 bits) */
    uint8_t  poll;            /* Poll interval (8 bits, signed power of 2) */
    int8_t   precision;       /* Clock precision (8 bits, signed power of 2) */
    uint32_t root_delay;      /* Root delay (32-bit fixed point 16.16) */
    uint32_t root_dispersion; /* Root dispersion (32-bit fixed point 16.16) */
    uint32_t ref_id;          /* Reference clock identifier */
    ntp_time_t ref_ts;        /* Reference Timestamp */
    ntp_time_t orig_ts;       /* Originate Timestamp (T1) */
    ntp_time_t recv_ts;       /* Receive Timestamp (T2) */
    ntp_time_t tx_ts;         /* Transmit Timestamp (T3) */
} ntp_packet_t;

/* Build a 48-byte NTP v3 client request packet */
void ntp_packet_init_request(uint8_t *buffer, size_t buf_size, ntp_time_t t1);

/* Parse a 48-byte NTP response packet */
bool ntp_packet_parse_response(const uint8_t *buffer, size_t len, ntp_packet_t *pkt);

#endif /* NTP_PACKET_H */
