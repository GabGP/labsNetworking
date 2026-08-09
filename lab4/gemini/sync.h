#ifndef SYNC_H
#define SYNC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "ntp_time.h"
#include "ntp_net.h"

typedef struct {
    ntp_time_t base_ntp;        /* Base NTP time when clock started */
    struct timespec mono_start; /* Monotonic start time */
    double cumulative_adj_sec;  /* Cumulative adjustment applied in seconds */
} sim_clock_t;

/* Initialize internal simulated clock with initial NTP base time */
void sim_clock_init(ntp_time_t initial_ntp);

/* Get current simulated clock time (Base + Monotonic Elapsed + Adjustments) */
ntp_time_t sim_clock_get_time(void);

/* Apply offset correction to simulated clock (offset in milliseconds) */
void sim_clock_adjust_ms(double offset_ms);

/* Find index of response with smallest delay (returns -1 if none valid) */
int sync_find_best_server(const ntp_server_result_t *results, size_t count);

#endif /* SYNC_H */
