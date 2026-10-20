/*
 * Keyless WEP station for Lab #10 (ESP32-C3 Super Mini, ESP-IDF).
 *
 * Joins the GL router's WEP network with a deliberately wrong key. Open System
 * authentication and association do not check the key, so the access point can
 * accept this device as a station. Power save is off, so the access point does
 * not hold frames for a sleeping station. Every 10 s the log shows the state and
 * the data frames the access point has sent to this device, counted in
 * promiscuous mode, to compare with the laptop's capture. While it is not
 * connected, the log shows how strongly the router is heard and whether the
 * router has replied to this device.
 *
 * The serial console (the same USB port as the monitor) changes the network name,
 * BSSID, channel and key while the board runs, so no rebuild is needed. See
 * keyless_console.c for the commands.
 *
 * Modules:
 *   keyless_config   defaults and the settings the console can change
 *   keyless_wifi     joining the router and applying new settings
 *   keyless_monitor  promiscuous-mode counters
 *   keyless_console  serial commands
 *   keyless_status   the 10-second status line
 */
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi_default.h"
#include "nvs_flash.h"

#include "keyless_config.h"
#include "keyless_console.h"
#include "keyless_monitor.h"
#include "keyless_status.h"
#include "keyless_wifi.h"

static keyless_sta_config_t s_config;

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    keyless_config_set_defaults(&s_config);
    keyless_monitor_init();
    keyless_wifi_start(&s_config);
    keyless_console_start(&s_config);
    keyless_status_loop();
}
