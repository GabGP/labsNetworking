/*
 * The status loop: prints one line every 10 s, and keeps the station trying to join while it is
 * not joined. It never returns.
 */
#ifndef KEYLESS_STATUS_H
#define KEYLESS_STATUS_H

void keyless_status_loop(void);

#endif
