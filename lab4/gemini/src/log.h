#ifndef LOG_H
#define LOG_H

#include <stddef.h>
#include <stdbool.h>
#include "ntp_time.h"
#include "ntp_net.h"

void log_banner(void);

void log_config(const char *initial_date_str,
                double threshold_ms,
                int interval_sec,
                size_t server_count,
                const char **servers);

void log_iteration_start(int iteration, ntp_time_t current_sim_time);

void log_server_result(size_t index, const ntp_server_result_t *res);

void log_iteration_summary(const ntp_server_result_t *results,
                           size_t count,
                           int best_idx,
                           ntp_time_t sim_before,
                           ntp_time_t sim_after,
                           double threshold_ms,
                           bool is_sync);

void log_verification_start(ntp_time_t current_sim_time);

#endif /* LOG_H */
