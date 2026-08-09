#ifndef NTP_NET_H
#define NTP_NET_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "ntp_time.h"
#include "ntp_packet.h"

#define MAX_SERVERS 16
#define DEFAULT_NTP_PORT "123"
#define DEFAULT_TIMEOUT_SEC 3

typedef struct {
    char hostname[64];
    char ip_str[64];
    bool responded;
    char error_msg[128];

    ntp_time_t t1; /* Client send time (simulated clock) */
    ntp_time_t t2; /* Server receive time (from reply) */
    ntp_time_t t3; /* Server transmit time (from reply) */
    ntp_time_t t4; /* Client receive time (simulated clock) */

    double delay_ms;
    double offset_ms;
    uint8_t stratum;
} ntp_server_result_t;

/* Default server list containing 10+ standard NTP hosts */
extern const char *DEFAULT_NTP_SERVERS[];
extern const size_t DEFAULT_NTP_SERVER_COUNT;

/* Function pointer type for obtaining current simulated time */
typedef ntp_time_t (*get_sim_time_fn)(void);

/* Query multiple NTP servers concurrently and record T1..T4 */
size_t ntp_query_all_servers(const char **hostnames,
                            size_t count,
                            int timeout_sec,
                            get_sim_time_fn sim_time_cb,
                            ntp_server_result_t *results);

#endif /* NTP_NET_H */
