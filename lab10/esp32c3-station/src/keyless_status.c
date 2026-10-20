#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "keyless_monitor.h"
#include "keyless_status.h"
#include "keyless_wifi.h"

#define TAG "keyless"

void keyless_status_loop(void)
{
    keyless_counters_t counters;
    uint32_t last_bcast = 0;
    uint32_t last_unicast = 0;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        long long up = esp_timer_get_time() / 1000000LL;
        keyless_monitor_read(&counters);
        if (keyless_wifi_connected()) {
            wifi_ap_record_t ap = {0};
            esp_wifi_sta_get_ap_info(&ap);
            ESP_LOGI(TAG, "t=%llds connected, rssi %d, AP broadcast data frames +%lu (total %lu), "
                     "AP unicast data frames +%lu (total %lu), disconnects %lu",
                     up, ap.rssi,
                     (unsigned long)(counters.broadcast_frames - last_bcast),
                     (unsigned long)counters.broadcast_frames,
                     (unsigned long)(counters.unicast_frames - last_unicast),
                     (unsigned long)counters.unicast_frames,
                     (unsigned long)keyless_wifi_disconnects());
        } else {
            ESP_LOGW(TAG, "t=%llds not connected (last reason %d), retrying",
                     up, keyless_wifi_last_reason());
            ESP_LOGI(TAG, "router beacons %lu, last RSSI %d dBm; replies from the router %lu (last status %u); "
                     "deauth or disassoc from the router %lu (last reason %u)",
                     (unsigned long)counters.beacons, counters.beacon_rssi,
                     (unsigned long)counters.auth_replies, (unsigned)counters.last_auth_status,
                     (unsigned long)counters.ap_deauths, (unsigned)counters.last_ap_deauth_reason);
            keyless_wifi_retry();
        }
        last_bcast = counters.broadcast_frames;
        last_unicast = counters.unicast_frames;
    }
}
