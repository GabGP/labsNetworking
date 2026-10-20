#include <string.h>

#include "keyless_config.h"

void keyless_config_set_defaults(keyless_sta_config_t *cfg)
{
    static const uint8_t default_bssid[6] = KEYLESS_DEFAULT_BSSID;

    memset(cfg, 0, sizeof *cfg);
    strcpy(cfg->ssid, KEYLESS_DEFAULT_SSID);
    strcpy(cfg->key, KEYLESS_DEFAULT_KEY);
    cfg->channel = KEYLESS_DEFAULT_CHANNEL;
    cfg->bssid_set = true;
    memcpy(cfg->bssid, default_bssid, sizeof cfg->bssid);
}
