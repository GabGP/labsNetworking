#include <stdint.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "keyless_config.h"
#include "keyless_monitor.h"
#include "keyless_wifi.h"

#define TAG "keyless"

static volatile bool s_connected;
static volatile uint8_t s_last_reason;
static volatile uint32_t s_disconnects;

/* Turns the settings into the driver's config. The threshold is WEP, so the station joins this
 * WEP network and never falls back to WPA2. */
static void build_wifi_config(const keyless_sta_config_t *cfg, wifi_config_t *out)
{
    memset(out, 0, sizeof *out);
    memcpy(out->sta.ssid, cfg->ssid, strlen(cfg->ssid));
    memcpy(out->sta.password, cfg->key, strlen(cfg->key));
    out->sta.channel = cfg->channel;
    out->sta.bssid_set = cfg->bssid_set;
    memcpy(out->sta.bssid, cfg->bssid, sizeof out->sta.bssid);
    out->sta.scan_method = WIFI_FAST_SCAN;
    out->sta.threshold.authmode = WIFI_AUTH_WEP;
    out->sta.pmf_cfg.capable = false;
    out->sta.pmf_cfg.required = false;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base != WIFI_EVENT) {
        return;
    }
    switch (id) {
    case WIFI_EVENT_STA_START: {
        /* Set before the first connect: full power failed to join on this board. */
        esp_err_t err = esp_wifi_set_max_tx_power(KEYLESS_DEFAULT_TX_DBM * 4);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "could not set TX power: %s", esp_err_to_name(err));
        }
        esp_wifi_connect();
        break;
    }
    case WIFI_EVENT_STA_CONNECTED: {
        const wifi_event_sta_connected_t *e = data;
        s_connected = true;
        keyless_monitor_set_bssid(e->bssid);
        /* Promiscuous mode is turned on only now, after the join. */
        keyless_monitor_enable();
        ESP_LOGI(TAG, "associated with %02x:%02x:%02x:%02x:%02x:%02x on channel %d",
                 e->bssid[0], e->bssid[1], e->bssid[2], e->bssid[3], e->bssid[4], e->bssid[5],
                 e->channel);
        break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
        const wifi_event_sta_disconnected_t *e = data;
        s_connected = false;
        keyless_monitor_disable();
        s_last_reason = e->reason;
        s_disconnects++;
        ESP_LOGW(TAG, "disconnected, reason %d", e->reason);
        break;
    }
    default:
        break;
    }
}

void keyless_wifi_start(const keyless_sta_config_t *cfg)
{
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    ESP_LOGI(TAG, "station MAC %02x:%02x:%02x:%02x:%02x:%02x, target \"%s\" channel %u, key of %u characters",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             cfg->ssid, (unsigned)cfg->channel, (unsigned)strlen(cfg->key));

    wifi_config_t wifi_cfg;
    build_wifi_config(cfg, &wifi_cfg);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    uint8_t protocol = 0;
    int8_t max_power = 0;
    ESP_ERROR_CHECK(esp_wifi_get_protocol(WIFI_IF_STA, &protocol));
    ESP_ERROR_CHECK(esp_wifi_get_max_tx_power(&max_power));
    ESP_LOGI(TAG, "protocol bitmap %u (1 = 11b, 2 = 11g, 4 = 11n), max TX power %d quarter dBm",
             protocol, max_power);
}

esp_err_t keyless_wifi_apply(const keyless_sta_config_t *cfg)
{
    wifi_config_t wifi_cfg;
    build_wifi_config(cfg, &wifi_cfg);
    keyless_monitor_disable();
    esp_wifi_disconnect();
    /* Wait for the disconnect before the config changes, so the driver takes the new settings. */
    for (int i = 0; i < 30 && s_connected; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (err == ESP_OK) {
        esp_wifi_connect();
    }
    return err;
}

void keyless_wifi_retry(void)
{
    esp_wifi_connect();
}

bool keyless_wifi_connected(void)
{
    return s_connected;
}

uint8_t keyless_wifi_last_reason(void)
{
    return s_last_reason;
}

uint32_t keyless_wifi_disconnects(void)
{
    return s_disconnects;
}
