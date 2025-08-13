// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * datetime.c: Date and time facilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "datetime.h"

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>

/** Time conversions. */
static const uint64_t datetime_nsecs_msec = 1000000;
static const uint64_t datetime_msecs_day = 86400000;
static const uint64_t datetime_msecs_hour = 3600000;
static const uint64_t datetime_msecs_min = 60000;

/**
 * Get the calendar time for the UTC timezone.
 *
 * @return Unix timestamp in milliseconds since the epoch, or 0 on failure.
 */
uint64_t tsig_datetime_get_timestamp(void) {
  struct timespec ts;

  /* C11 timespec_get() is still not universally supported. */
  if (clock_gettime(CLOCK_REALTIME, &ts))
    return 0;

  /* tv_sec should not be negative. Also, it might not be 64 bits. */
  return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / datetime_nsecs_msec;
}

/**
 * Parse a timestamp into a date and time.
 *
 * @param timestamp Unix timestamp in milliseconds.
 * @return A tsig_datetime_t structure.
 */
tsig_datetime_t tsig_datetime_parse_timestamp(int64_t timestamp) {
  tsig_datetime_t datetime = {.timestamp = timestamp};
  uint64_t msec = timestamp;

  /*
   * Certain date calculations are simplified by shifting the
   * epoch to begin on March 1, 0000 instead of January 1, 1970.
   * cf. https://howardhinnant.github.io/date_algorithms.html
   */

  uint64_t day = msec / datetime_msecs_day;
  uint64_t dse = day + 719468;
  uint32_t era = dse / 146097;
  uint32_t doe = dse - era * 146097;
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  uint32_t y = yoe + era * 400;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t m = (5 * doy + 2) / 153;

  datetime.year = y + (m >= 10);
  datetime.mon = m < 10 ? m + 3 : m - 9;      /* 1-12 */
  datetime.day = doy - (153 * m + 2) / 5 + 1; /* 1-31 */
  datetime.doy =
      m < 10 ? doy + 60 + tsig_datetime_is_leap(datetime.year) : doy - 305;
  datetime.dow = (day + 4) % 7;
  datetime.hour = (msec %= datetime_msecs_day) / datetime_msecs_hour;
  datetime.min = (msec %= datetime_msecs_hour) / datetime_msecs_min;
  datetime.sec = (msec %= datetime_msecs_min) / 1000;
  datetime.msec = msec % 1000;

  return datetime;
}

/**
 * Determine whether a year is a leap year.
 *
 * @param year Gregorian year.
 * @return Whether the year is a Gregorian leap year.
 */
bool tsig_datetime_is_leap(uint16_t year) {
  return !(year % 4) && ((year % 100) || !(year % 400));
}

/**
 * Check if Summer Time is in effect in Germany or the United Kingdom.
 *
 * For Germany and the UK, as well as for many other countries in Europe,
 * summer time begins/ends at 01:00 UTC on the last Sunday of March/October.
 *
 * @param datetime UTC datetime to be checked.
 * @param[out] out_in_mins Optional out pointer to the count of minutes
 *  remaining until the next changeover as of the beginning of the minute
 *  in `datetime` (i.e. 1 means the time in `datetime` is 00:59:XX UTC,
 *  and the changeover will occur in <= 60 seconds). If the changeover will
 * occur in more than 25 hours, -1 is written to the pointer.
 * @return Whether CEST/BST are in effect in Germany/the UK at `datetime`.
 */
bool tsig_datetime_is_eu_dst(tsig_datetime_t datetime, int32_t *out_in_mins) {
  uint8_t mon = datetime.mon;
  int32_t in_mins = -1;
  bool is_est = false;

  if (3 < mon && mon < 10) {
    is_est = true;
  } else if (mon == 3 || mon == 10) {
    uint8_t hour = datetime.hour;
    uint8_t min = datetime.min;
    uint8_t day = datetime.day;
    uint8_t dow = datetime.dow;

    uint8_t fsom = (((day - 1) + (dow ? 7 - dow : 0)) % 7) + 1;
    uint8_t lsom = fsom + ((31 - fsom) / 7) * 7;
    bool is_changed = (day == lsom && hour >= 1) || day > lsom;

    is_est = (mon == 3) == is_changed;

    if (day == lsom - 1)
      in_mins = 60 * (24 - hour) + 60 - min;
    else if (day == lsom && hour < 1)
      in_mins = 60 - min;
  }

  if (out_in_mins)
    *out_in_mins = in_mins;

  return is_est;
}

/**
 * Check if Daylight Saving Time is in effect in the United States.
 *
 * Daylight Saving Time begins/ends at 02:00 local time on the second Sunday
 * of March/the first Sunday of November.
 *
 * @param datetime UTC datetime to be checked.
 * @param[out] out_is_dst_end Optional out pointer to whether DST will be in
 *  effect at the end of the provided UTC day.
 * @return Whether DST is in effect in the United States at the beginning of
 *  the provided UTC day.
 */
bool tsig_datetime_is_us_dst(tsig_datetime_t datetime, bool *out_is_dst_end) {
  uint8_t mon = datetime.mon;
  bool is_dst_end = false;
  bool is_dst = false;

  if (3 < mon && mon < 11) {
    is_dst_end = true;
    is_dst = true;
  } else if (mon == 3 || mon == 11) {
    uint8_t sunday = mon == 3 ? 8 : 1;
    uint8_t day = datetime.day;
    uint8_t dow = datetime.dow;

    uint8_t change_day = (((day - 1) + (dow ? 7 - dow : 0)) % 7) + sunday;
    is_dst_end = mon == 3 ? day >= change_day : day < change_day;
    is_dst = mon == 3 ? day > change_day : day <= change_day;
  }

  if (out_is_dst_end)
    *out_is_dst_end = is_dst_end;

  return is_dst;
}
