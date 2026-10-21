/*
 * Serial console, built on the ESP-IDF console (a REPL) over the USB serial port. Type a command and
 * press Enter; the board answers on the same port. Settings only take effect after apply. Each
 * command also has a one-letter short form, shown first below.
 *
 *   h   help                        list the commands
 *   s   status                      the settings, and whether the station is joined
 *   n   ssid <name>                 network name, up to 32 characters
 *   k   key <key>                   WEP key, 5 or 13 ASCII characters (never printed back)
 *   c   channel <0-13>              channel, 0 scans every channel
 *   b   bssid <aa:bb:cc:dd:ee:ff>   lock to one access point, or: bssid any
 *   a   apply                       leave the network and join with these settings
 *   t   txpower <2-20>              max transmit power in dBm (the boot default is 20)
 *   p   pause                       hide the log lines so you can type a command
 *   r   resume                      show the log lines again
 */
#include <stdio.h>
#include <string.h>

#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "keyless_console.h"
#include "keyless_wifi.h"

static keyless_sta_config_t *s_cfg;

static int cmd_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const uint8_t *b = s_cfg->bssid;
    printf("ssid \"%s\", channel %u, bssid ", s_cfg->ssid, (unsigned)s_cfg->channel);
    if (s_cfg->bssid_set) {
        printf("%02x:%02x:%02x:%02x:%02x:%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
    } else {
        printf("any");
    }
    printf(", key of %u characters, %s\n", (unsigned)strlen(s_cfg->key),
           keyless_wifi_connected() ? "joined" : "not joined");
    int8_t max_power = 0;
    esp_wifi_get_max_tx_power(&max_power);
    printf("max TX power %d quarter dBm\n", max_power);
    return 0;
}

static int cmd_ssid(int argc, char **argv)
{
    if (argc != 2 || strlen(argv[1]) > 32) {
        printf("usage: ssid <name>, up to 32 characters\n");
        return 1;
    }
    strcpy(s_cfg->ssid, argv[1]);
    printf("ssid set, type apply to join\n");
    return 0;
}

static int cmd_key(int argc, char **argv)
{
    size_t n = (argc == 2) ? strlen(argv[1]) : 0;
    if (n != 5 && n != 13) {
        printf("usage: key <5 or 13 characters>\n");
        return 1;
    }
    strcpy(s_cfg->key, argv[1]);
    printf("key set (%u characters), type apply to join\n", (unsigned)n);
    return 0;
}

static int cmd_channel(int argc, char **argv)
{
    unsigned channel = 0;
    if (argc != 2 || sscanf(argv[1], "%u", &channel) != 1 || channel > 13) {
        printf("usage: channel <0-13>, 0 scans every channel\n");
        return 1;
    }
    s_cfg->channel = (uint8_t)channel;
    printf("channel set, type apply to join\n");
    return 0;
}

static int cmd_bssid(int argc, char **argv)
{
    unsigned b[6] = { 0 };
    if (argc == 2 && strcmp(argv[1], "any") == 0) {
        s_cfg->bssid_set = false;
        printf("bssid set to any, type apply to join\n");
        return 0;
    }
    if (argc != 2 ||
        sscanf(argv[1], "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
        printf("usage: bssid <aa:bb:cc:dd:ee:ff> or bssid any\n");
        return 1;
    }
    for (int i = 0; i < 6; i++) {
        s_cfg->bssid[i] = (uint8_t)b[i];
    }
    s_cfg->bssid_set = true;
    printf("bssid set, type apply to join\n");
    return 0;
}

static int cmd_apply(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    esp_err_t err = keyless_wifi_apply(s_cfg);
    if (err != ESP_OK) {
        printf("could not apply: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("applied, joining\n");
    return 0;
}

static int cmd_txpower(int argc, char **argv)
{
    int dbm = 0;
    if (argc != 2 || sscanf(argv[1], "%d", &dbm) != 1 || dbm < 2 || dbm > 20) {
        printf("usage: txpower <2-20 dBm>\n");
        return 1;
    }
    esp_err_t err = esp_wifi_set_max_tx_power((int8_t)(dbm * 4));
    if (err != ESP_OK) {
        printf("could not set TX power: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("max TX power set to %d dBm, type apply to rejoin\n", dbm);
    return 0;
}

/* Hides the periodic log lines so a command typed over them is easy to read. Errors still show. */
static int cmd_pause(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    esp_log_level_set("*", ESP_LOG_ERROR);
    printf("logs hidden, type resume to show them again\n");
    return 0;
}

static int cmd_resume(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    esp_log_level_set("*", ESP_LOG_INFO);
    printf("logs shown again\n");
    return 0;
}

/* ESP-IDF keeps its help function private, so the short form runs help by name. */
static int cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int ret = 0;
    esp_err_t err = esp_console_run("help", &ret);
    if (err != ESP_OK) {
        printf("could not run help: %s\n", esp_err_to_name(err));
        return 1;
    }
    return ret;
}

/* The command table. The console's own help command lists the entries with help text. The short forms
 * at the end have none, so help leaves them out. */
static const esp_console_cmd_t s_commands[] = {
    { .command = "status", .help = "[s] show the settings and whether the station is joined", .func = &cmd_status },
    { .command = "ssid", .help = "[n] set the network name, up to 32 characters", .func = &cmd_ssid },
    { .command = "key", .help = "[k] set the WEP key, 5 or 13 characters (never printed back)", .func = &cmd_key },
    { .command = "channel", .help = "[c] set the channel, 0-13 (0 scans every channel)", .func = &cmd_channel },
    { .command = "bssid", .help = "[b] lock to one AP (aa:bb:cc:dd:ee:ff) or 'bssid any'", .func = &cmd_bssid },
    { .command = "txpower", .help = "[t] set the max transmit power in dBm, 2-20", .func = &cmd_txpower },
    { .command = "apply", .help = "[a] leave the network and join with the current settings", .func = &cmd_apply },
    { .command = "pause", .help = "[p] hide the log lines so you can type a command", .func = &cmd_pause },
    { .command = "resume", .help = "[r] show the log lines again", .func = &cmd_resume },
    { .command = "h", .func = &cmd_help },
    { .command = "s", .func = &cmd_status },
    { .command = "n", .func = &cmd_ssid },
    { .command = "k", .func = &cmd_key },
    { .command = "c", .func = &cmd_channel },
    { .command = "b", .func = &cmd_bssid },
    { .command = "t", .func = &cmd_txpower },
    { .command = "a", .func = &cmd_apply },
    { .command = "p", .func = &cmd_pause },
    { .command = "r", .func = &cmd_resume },
};

void keyless_console_start(keyless_sta_config_t *cfg)
{
    s_cfg = cfg;

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "keyless> ";
    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));
    ESP_ERROR_CHECK(esp_console_register_help_command());
    for (size_t i = 0; i < sizeof s_commands / sizeof s_commands[0]; i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&s_commands[i]));
    }
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
