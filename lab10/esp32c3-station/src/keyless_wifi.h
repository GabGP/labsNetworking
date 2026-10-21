/*
 * The Wi-Fi station: joins the router, applies new settings from the console, and reports
 * whether it is joined.
 */
#ifndef KEYLESS_WIFI_H
#define KEYLESS_WIFI_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "keyless_config.h"

/* Starts the station with these settings. The join starts when the driver starts. */
void keyless_wifi_start(const keyless_sta_config_t *cfg);

/* Leaves the current network, takes these settings, and joins again. */
esp_err_t keyless_wifi_apply(const keyless_sta_config_t *cfg);

/* Sets the transmit power the station runs at once joined, in dBm. Joins use the lower of this and
 * the join power. */
esp_err_t keyless_wifi_set_tx_dbm(int dbm);
/* The transmit power the station runs at once joined, in dBm. */
int keyless_wifi_tx_dbm(void);

/* Asks the driver to join again. The status loop calls this while the station is not joined. */
void keyless_wifi_retry(void);

bool keyless_wifi_connected(void);
uint8_t keyless_wifi_last_reason(void);
uint32_t keyless_wifi_disconnects(void);

#endif
