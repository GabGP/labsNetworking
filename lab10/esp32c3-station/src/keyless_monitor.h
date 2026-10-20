/*
 * Counts what the router sends to this station, in promiscuous mode. Promiscuous mode is turned on
 * only once the station has joined: turned on before the join, it stops the join.
 */
#ifndef KEYLESS_MONITOR_H
#define KEYLESS_MONITOR_H

#include <stdint.h>

typedef struct {
    uint32_t beacons;
    int8_t beacon_rssi;
    uint32_t auth_replies;
    uint16_t last_auth_status;
    uint32_t ap_deauths;
    uint16_t last_ap_deauth_reason;
    uint32_t broadcast_frames;
    uint32_t unicast_frames;
} keyless_counters_t;

/* Reads this station's MAC, which the counters use to recognise replies sent to it. */
void keyless_monitor_init(void);

/* Sets the access point whose frames are counted. Called when the station joins. */
void keyless_monitor_set_bssid(const uint8_t *bssid);

/* Turns promiscuous mode on or off. Does nothing if it is already in that state. */
void keyless_monitor_enable(void);
void keyless_monitor_disable(void);

/* Copies the counters. */
void keyless_monitor_read(keyless_counters_t *out);

#endif
