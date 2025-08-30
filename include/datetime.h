/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * datetime.h: Header for date and time facilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * Date and time. Presented in a friendlier manner than a raw timestamp.
 * @note The original timestamp used to initialize this struct is found
 * in `timestamp`. Partial milliseconds are not preserved in `msec`.
 */
typedef struct tsig_datetime_t {
  uint64_t timestamp; /** Unix timestamp in milliseconds. */

  uint16_t year; /** Year (0 and up). */
  uint8_t mon;   /** Month (1-12). */
  uint8_t day;   /** Day of month (1-31). */
  uint16_t doy;  /** Day of year (1-366). */
  uint8_t dow;   /** Day of week (0-6, Sunday-Saturday). */

  uint8_t hour;  /** Hour (0-23). */
  uint8_t min;   /** Minute (0-59). */
  uint8_t sec;   /** Second (0-59). */
  uint16_t msec; /** Millisecond (0-999). */
} tsig_datetime_t;

uint64_t tsig_datetime_get_timestamp(void);
tsig_datetime_t tsig_datetime_parse_timestamp(int64_t timestamp);
int64_t tsig_datetime_make_timestamp(uint16_t year, uint8_t mon, uint8_t day,
                                     uint8_t hour, uint8_t min, uint8_t sec,
                                     uint16_t msec, int16_t tz);
bool tsig_datetime_is_leap(uint16_t year);
uint8_t tsig_datetime_days_in_mon(uint16_t year, uint8_t mon);
bool tsig_datetime_is_eu_dst(tsig_datetime_t datetime, int32_t *out_in_mins);
bool tsig_datetime_is_us_dst(tsig_datetime_t datetime, bool *out_is_dst_end);
