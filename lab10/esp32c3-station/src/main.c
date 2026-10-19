/*
 * Keyless WEP station for Lab #10 (ESP32-C3 Super Mini, ESP-IDF).
 *
 * Joins the GL router's WEP network with a deliberately wrong key. Open System
 * authentication and association do not check the key, so the access point can
 * accept this device as a station. Power save is off, so the access point does
 * not hold frames for a sleeping station. Every 10 s the log shows the state and
 * the data frames the access point has sent to this device, counted in
 * promiscuous mode, to compare with the laptop's capture.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define TAG "keyless"

/* The lab router: SSID "Test" on channel 6. Locking the BSSID keeps other APs out. */
#define TARGET_SSID "Test"
#define TARGET_BSSID {0x94, 0x83, 0xC4, 0x87, 0x50, 0x8F}
#define TARGET_CHANNEL 6
/* 13 ASCII characters on purpose, and not the router's key. */
#define WRONG_KEY "<redacted>"

static const uint8_t s_bssid[6] = TARGET_BSSID;
static uint8_t s_own_mac[6];
static volatile bool s_connected;
static volatile uint8_t s_last_reason;
static volatile uint32_t s_disconnects;
static volatile uint32_t s_broadcast_frames;
static volatile uint32_t s_unicast_frames;

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base != WIFI_EVENT) {
        return;
    }
    switch (id) {
    case WIFI_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_CONNECTED: {
        const wifi_event_sta_connected_t *e = data;
        s_connected = true;
        ESP_LOGI(TAG, "associated with %02x:%02x:%02x:%02x:%02x:%02x on channel %d",
                 e->bssid[0], e->bssid[1], e->bssid[2], e->bssid[3], e->bssid[4], e->bssid[5],
                 e->channel);
        break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
        const wifi_event_sta_disconnected_t *e = data;
        s_connected = false;
        s_last_reason = e->reason;
        s_disconnects++;
        ESP_LOGW(TAG, "disconnected, reason %d", e->reason);
        break;
    }
    default:
        break;
    }
}

/* Counts data frames sent by the access point to broadcast or to this device. */
static void promiscuous_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    (void)type;
    const wifi_promiscuous_pkt_t *pkt = buf;
    const uint8_t *f = pkt->payload;
    if (pkt->rx_ctrl.sig_len < 24) {
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

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_read_mac(s_own_mac, ESP_MAC_WIFI_STA));
    ESP_LOGI(TAG, "station MAC %02x:%02x:%02x:%02x:%02x:%02x, target \"%s\" channel %d, WEP with a wrong key",
             s_own_mac[0], s_own_mac[1], s_own_mac[2], s_own_mac[3], s_own_mac[4], s_own_mac[5],
             TARGET_SSID, TARGET_CHANNEL);

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = TARGET_SSID,
            .password = WRONG_KEY,
            .bssid_set = true,
            .bssid = TARGET_BSSID,
            .channel = TARGET_CHANNEL,
            .scan_method = WIFI_FAST_SCAN,
            .threshold.authmode = WIFI_AUTH_WEP,
            .pmf_cfg = { .capable = false, .required = false },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    wifi_promiscuous_filter_t filter = { .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA };
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filter));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(promiscuous_cb));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));

    uint32_t last_bcast = 0;
    uint32_t last_unicast = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        long long up = esp_timer_get_time() / 1000000LL;
        uint32_t bcast = s_broadcast_frames;
        uint32_t unicast = s_unicast_frames;
        if (s_connected) {
            wifi_ap_record_t ap = {0};
            esp_wifi_sta_get_ap_info(&ap);
            ESP_LOGI(TAG, "t=%llds connected, rssi %d, AP broadcast data frames +%lu (total %lu), "
                     "AP unicast data frames +%lu (total %lu), disconnects %lu",
                     up, ap.rssi, (unsigned long)(bcast - last_bcast), (unsigned long)bcast,
                     (unsigned long)(unicast - last_unicast), (unsigned long)unicast,
                     (unsigned long)s_disconnects);
        } else {
            ESP_LOGW(TAG, "t=%llds not connected (last reason %d), retrying", up, s_last_reason);
            esp_wifi_connect();
        }
        last_bcast = bcast;
        last_unicast = unicast;
    }
}
