#define _POSIX_C_SOURCE 200809L
#include "ntp_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

uint64_t ntp_time_to_u64(ntp_time_t t) {
    return ((uint64_t)t.seconds << 32) | (uint64_t)t.fraction;
}

ntp_time_t u64_to_ntp_time(uint64_t val) {
    ntp_time_t t;
    t.seconds = (uint32_t)(val >> 32);
    t.fraction = (uint32_t)(val & 0xFFFFFFFFULL);
    return t;
}

ntp_time_t double_to_ntp_time(double sec) {
    ntp_time_t t;
    if (sec < 0.0) {
        t.seconds = 0;
        t.fraction = 0;
        return t;
    }
    double int_part;
    double frac_part = modf(sec, &int_part);
    t.seconds = (uint32_t)int_part;

    double frac_scaled = round(frac_part * 4294967296.0);
    if (frac_scaled >= 4294967296.0) {
        t.seconds += 1;
        t.fraction = 0;
    } else {
        t.fraction = (uint32_t)frac_scaled;
    }
    return t;
}

double ntp_time_to_double(ntp_time_t t) {
    return (double)t.seconds + ((double)t.fraction / 4294967296.0);
}

ntp_time_t unix_to_ntp_time(time_t unix_sec, double frac_sec) {
    ntp_time_t t;
    t.seconds = (uint32_t)(unix_sec + NTP_EPOCH_DELTA);
    
    double frac_scaled = round(frac_sec * 4294967296.0);
    if (frac_scaled >= 4294967296.0) {
        t.seconds += 1;
        t.fraction = 0;
    } else {
        t.fraction = (uint32_t)frac_scaled;
    }
    return t;
}

static bool is_leap(int year) {
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

static time_t utc_tm_to_time_t(const struct tm *tm) {
    int year = tm->tm_year + 1900;
    int month = tm->tm_mon + 1; /* 1 - 12 */
    int day = tm->tm_mday;

    static const int days_before_month[13] = {
        0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };

    long days = (year - 1970) * 365;
    for (int y = 1970; y < year; y++) {
        if (is_leap(y)) days++;
    }
    days += days_before_month[month];
    if (month > 2 && is_leap(year)) days++;
    days += (day - 1);

    time_t sec = days * 86400L + tm->tm_hour * 3600L + tm->tm_min * 60L + tm->tm_sec;
    return sec;
}

bool parse_guatemala_time(const char *str, ntp_time_t *out_ntp) {
    if (!str || !out_ntp) return false;

    int year = 0, month = 0, day = 0;
    int hour = 0, min = 0, sec = 0, ms = 0;

    int scanned = sscanf(str, "%d-%d-%d %d:%d:%d.%d", &year, &month, &day, &hour, &min, &sec, &ms);
    if (scanned < 6) {
        return false;
    }
    if (scanned == 6) {
        ms = 0;
    }

    struct tm tm_local;
    memset(&tm_local, 0, sizeof(tm_local));
    tm_local.tm_year = year - 1900;
    tm_local.tm_mon  = month - 1;
    tm_local.tm_mday = day;
    tm_local.tm_hour = hour;
    tm_local.tm_min  = min;
    tm_local.tm_sec  = sec;

    /* Local Guatemala time to UTC: add 6 hours */
    time_t local_unix_sec = utc_tm_to_time_t(&tm_local);
    time_t utc_unix_sec = local_unix_sec + GUATEMALA_UTC_OFFSET_SEC;

    double frac_sec = (double)ms / 1000.0;
    *out_ntp = unix_to_ntp_time(utc_unix_sec, frac_sec);
    return true;
}

void ntp_time_to_guatemala_str(ntp_time_t ntp, char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;

    if (ntp.seconds < NTP_EPOCH_DELTA) {
        snprintf(buf, buf_size, "INVALID_NTP_TIME");
        return;
    }

    time_t utc_unix_sec = (time_t)(ntp.seconds - NTP_EPOCH_DELTA);
    /* Guatemala local time = UTC - 6 hours */
    time_t guatemala_sec = utc_unix_sec - GUATEMALA_UTC_OFFSET_SEC;

    struct tm tm_guat;
    gmtime_r(&guatemala_sec, &tm_guat);

    uint32_t ms = (uint32_t)round(((double)ntp.fraction / 4294967296.0) * 1000.0);
    if (ms >= 1000) {
        ms = 999;
    }

    snprintf(buf, buf_size, "%04d-%02d-%02d %02d:%02d:%02d.%03u (Guatemala)",
             tm_guat.tm_year + 1900, tm_guat.tm_mon + 1, tm_guat.tm_mday,
             tm_guat.tm_hour, tm_guat.tm_min, tm_guat.tm_sec, ms);
}

void ntp_time_to_utc_str(ntp_time_t ntp, char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;

    if (ntp.seconds < NTP_EPOCH_DELTA) {
        snprintf(buf, buf_size, "INVALID_NTP_TIME");
        return;
    }

    time_t utc_unix_sec = (time_t)(ntp.seconds - NTP_EPOCH_DELTA);

    struct tm tm_utc;
    gmtime_r(&utc_unix_sec, &tm_utc);

    uint32_t ms = (uint32_t)round(((double)ntp.fraction / 4294967296.0) * 1000.0);
    if (ms >= 1000) {
        ms = 999;
    }

    snprintf(buf, buf_size, "%04d-%02d-%02d %02d:%02d:%02d.%03u UTC",
             tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
             tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec, ms);
}

double ntp_diff_ms(ntp_time_t t1, ntp_time_t t2) {
    int64_t v1 = (int64_t)ntp_time_to_u64(t1);
    int64_t v2 = (int64_t)ntp_time_to_u64(t2);
    int64_t diff = v2 - v1;
    return ((double)diff * 1000.0) / 4294967296.0;
}

double ntp_calc_offset_ms(ntp_time_t t1, ntp_time_t t2, ntp_time_t t3, ntp_time_t t4) {
    int64_t v1 = (int64_t)ntp_time_to_u64(t1);
    int64_t v2 = (int64_t)ntp_time_to_u64(t2);
    int64_t v3 = (int64_t)ntp_time_to_u64(t3);
    int64_t v4 = (int64_t)ntp_time_to_u64(t4);

    int64_t diff21 = v2 - v1;
    int64_t diff34 = v3 - v4;
    int64_t sum = diff21 + diff34;

    return ((double)sum * 1000.0) / (2.0 * 4294967296.0);
}

double ntp_calc_delay_ms(ntp_time_t t1, ntp_time_t t2, ntp_time_t t3, ntp_time_t t4) {
    int64_t v1 = (int64_t)ntp_time_to_u64(t1);
    int64_t v2 = (int64_t)ntp_time_to_u64(t2);
    int64_t v3 = (int64_t)ntp_time_to_u64(t3);
    int64_t v4 = (int64_t)ntp_time_to_u64(t4);

    int64_t diff41 = v4 - v1;
    int64_t diff32 = v3 - v2;
    int64_t delay_fixed = diff41 - diff32;

    return ((double)delay_fixed * 1000.0) / 4294967296.0;
}
