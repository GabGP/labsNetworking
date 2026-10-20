#include <stdbool.h>
#include <string.h>

#include "esp_err.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "keyless_monitor.h"

static volatile uint32_t s_beacons;
static volatile int8_t s_beacon_rssi;
static volatile uint32_t s_auth_replies;
static volatile uint16_t s_last_auth_status;
static volatile uint32_t s_ap_deauths;
static volatile uint16_t s_last_ap_deauth_reason;
static volatile uint32_t s_broadcast_frames;
static volatile uint32_t s_unicast_frames;
static uint8_t s_bssid[6];
static uint8_t s_own_mac[6];
static bool s_on;

/* Counts data frames sent by the access point to broadcast or to this station. Management frames
 * give the router's beacon signal and the replies it sends to this station. */
static void promiscuous_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    const wifi_promiscuous_pkt_t *pkt = buf;
    const uint8_t *f = pkt->payload;
    if (pkt->rx_ctrl.sig_len < 24) {
        return;
    }
    if (type == WIFI_PKT_MGMT) {
        if (memcmp(f + 10, s_bssid, 6) != 0) {
            return;
        }
        if (f[0] == 0x80) {
            s_beacons++;
            s_beacon_rssi = pkt->rx_ctrl.rssi;
        } else if (memcmp(f + 4, s_own_mac, 6) == 0) {
            if (f[0] == 0xB0 && pkt->rx_ctrl.sig_len >= 30) {
                s_auth_replies++;
                s_last_auth_status = f[28] | (f[29] << 8);
            } else if (f[0] == 0xC0 || f[0] == 0xA0) {
                s_ap_deauths++;
                s_last_ap_deauth_reason = f[24] | (f[25] << 8);
            }
        }
        return;
    }
    /* Type data, FromDS set and ToDS clear: the access point is the sender. */
    if (((f[0] >> 2) & 0x3) != 2 || (f[1] & 0x03) != 0x02) {
        return;
    }
    if (memcmp(f + 10, s_bssid, 6) != 0) {
        return;
    }
    if (f[4] & 0x01) {
        s_broadcast_frames++;
    } else if (memcmp(f + 4, s_own_mac, 6) == 0) {
        s_unicast_frames++;
    }
}

void keyless_monitor_init(void)
{
    ESP_ERROR_CHECK(esp_read_mac(s_own_mac, ESP_MAC_WIFI_STA));
}

void keyless_monitor_set_bssid(const uint8_t *bssid)
{
    memcpy(s_bssid, bssid, sizeof s_bssid);
}

void keyless_monitor_enable(void)
{
    if (s_on) {
        return;
    }
    wifi_promiscuous_filter_t filter = { .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA | WIFI_PROMIS_FILTER_MASK_MGMT };
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filter));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(promiscuous_cb));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    s_on = true;
}

void keyless_monitor_disable(void)
{
    if (!s_on) {
        return;
    }
    esp_wifi_set_promiscuous(false);
    s_on = false;
}

void keyless_monitor_read(keyless_counters_t *out)
{
    out->beacons = s_beacons;
    out->beacon_rssi = s_beacon_rssi;
    out->auth_replies = s_auth_replies;
    out->last_auth_status = s_last_auth_status;
    out->ap_deauths = s_ap_deauths;
    out->last_ap_deauth_reason = s_last_ap_deauth_reason;
    out->broadcast_frames = s_broadcast_frames;
    out->unicast_frames = s_unicast_frames;
}
