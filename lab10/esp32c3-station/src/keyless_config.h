/*
 * Station settings for the keyless WEP station. The defaults are the lab router. The serial
 * console can change any of them while the board runs (see keyless_console.c).
 */
#ifndef KEYLESS_CONFIG_H
#define KEYLESS_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/* Defaults for the lab router: SSID "Test" on channel 6, with the BSSID locked so other APs stay out. */
#define KEYLESS_DEFAULT_SSID "Test"
#define KEYLESS_DEFAULT_BSSID {0x94, 0x83, 0xC4, 0x87, 0x50, 0x8F}
#define KEYLESS_DEFAULT_CHANNEL 6
/* 13 ASCII characters on purpose, and not the router's key. */
#define KEYLESS_DEFAULT_KEY "wrongkey13chr"
/* Transmit power at boot, in dBm. At full power (20 dBm) this board's joins failed in the lab: the
 * router never confirmed them. 8 dBm joined every time. The console's txpower command changes it. */
#define KEYLESS_DEFAULT_TX_DBM 8

typedef struct {
    char ssid[33];
    char key[65];
    uint8_t channel;  /* 0 scans every channel */
    bool bssid_set;   /* false joins any access point with the SSID */
    uint8_t bssid[6];
} keyless_sta_config_t;

/* Fills cfg with the defaults above. */
void keyless_config_set_defaults(keyless_sta_config_t *cfg);

#endif
