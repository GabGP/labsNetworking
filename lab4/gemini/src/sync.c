#define _POSIX_C_SOURCE 200809L
#include "sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

static sim_clock_t g_sim_clock;

void sim_clock_init(ntp_time_t initial_ntp) {
    g_sim_clock.base_ntp = initial_ntp;
    g_sim_clock.cumulative_adj_sec = 0.0;
    clock_gettime(CLOCK_MONOTONIC, &g_sim_clock.mono_start);
}

ntp_time_t sim_clock_get_time(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    double elapsed_sec = (double)(now.tv_sec - g_sim_clock.mono_start.tv_sec) +
                         (double)(now.tv_nsec - g_sim_clock.mono_start.tv_nsec) / 1e9;

    double total_base_sec = ntp_time_to_double(g_sim_clock.base_ntp);
    double current_sim_sec = total_base_sec + elapsed_sec + g_sim_clock.cumulative_adj_sec;

    return double_to_ntp_time(current_sim_sec);
}

void sim_clock_adjust_ms(double offset_ms) {
    g_sim_clock.cumulative_adj_sec += (offset_ms / 1000.0);
}

int sync_find_best_server(const ntp_server_result_t *results, size_t count) {
    if (!results || count == 0) return -1;

    int best_index = -1;
    double min_delay = 1e12; /* Start with large value */

    for (size_t i = 0; i < count; i++) {
        if (!results[i].responded) continue;
        if (results[i].stratum == 0) continue;

        /* Standard NTP selection: smallest delay */
        if (results[i].delay_ms < min_delay) {
            min_delay = results[i].delay_ms;
            best_index = (int)i;
        }
    }

    return best_index;
}
