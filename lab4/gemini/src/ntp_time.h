#ifndef NTP_TIME_H
#define NTP_TIME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/* Seconds between 1900-01-01 00:00:00 UTC and 1970-01-01 00:00:00 UTC */
#define NTP_EPOCH_DELTA 2208988800ULL

/* Guatemala is UTC-6 all year round (no daylight saving time) */
/* UTC_time = Guatemala_local_time + 6 hours (21600 seconds) */
#define GUATEMALA_UTC_OFFSET_SEC 21600

typedef struct {
    uint32_t seconds;   /* Seconds since 1900-01-01 00:00:00 UTC */
    uint32_t fraction;  /* Fraction of second (units of 1/2^32) */
} ntp_time_t;

/* Conversions between ntp_time_t and 64-bit unsigned integer (fixed point 32.32) */
uint64_t ntp_time_to_u64(ntp_time_t t);
ntp_time_t u64_to_ntp_time(uint64_t val);

/* Conversions between ntp_time_t and floating point seconds */
ntp_time_t double_to_ntp_time(double sec);
double ntp_time_to_double(ntp_time_t t);

/* Unix timestamp to NTP timestamp */
ntp_time_t unix_to_ntp_time(time_t unix_sec, double frac_sec);

/* Parse Guatemala local date/time string "YYYY-MM-DD HH:MM:SS.mmm" */
bool parse_guatemala_time(const char *str, ntp_time_t *out_ntp);

/* String formatting for NTP timestamps */
void ntp_time_to_guatemala_str(ntp_time_t ntp, char *buf, size_t buf_size);
void ntp_time_to_utc_str(ntp_time_t ntp, char *buf, size_t buf_size);

/* Compute difference (t2 - t1) in milliseconds */
double ntp_diff_ms(ntp_time_t t1, ntp_time_t t2);

/* Calculate offset in ms: ((T2 - T1) + (T3 - T4)) / 2 */
double ntp_calc_offset_ms(ntp_time_t t1, ntp_time_t t2, ntp_time_t t3, ntp_time_t t4);

/* Calculate delay in ms: (T4 - T1) - (T3 - T2) */
double ntp_calc_delay_ms(ntp_time_t t1, ntp_time_t t2, ntp_time_t t3, ntp_time_t t4);

#endif /* NTP_TIME_H */
