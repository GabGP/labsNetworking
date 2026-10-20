/*
 * Serial console on the same USB port as the monitor. Type a command and press Enter; the board
 * answers on the same port. See keyless_console.c for the commands.
 */
#ifndef KEYLESS_CONSOLE_H
#define KEYLESS_CONSOLE_H

#include "keyless_config.h"

/* Starts the console task. Its commands change cfg, and apply joins with the new settings. */
void keyless_console_start(keyless_sta_config_t *cfg);

#endif
