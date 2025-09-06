// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * station.c: Time station waveform generator.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "station.h"

#include "cfg.h"
#include "datetime.h"
#include "log.h"
#include "mapping.h"

#include <syslog.h>

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/** First run indicator. */
static const uint64_t station_first_run = UINT64_MAX;

/** Maximum allowed time drift in milliseconds. */
static const uint64_t station_drift_threshold = 500;

/** Time conversions. */
static const uint32_t station_msecs_hour = 3600000;
static const uint32_t station_msecs_min = 60000;

/** Output gain smoothing. */
static const double station_lerp_rate = 0.015;
static const double station_lerp_min_delta = 0.005;

/** Sync marker for transmit level flags. */
static const uint8_t station_sync_marker = 0xff;

/* Subharmonic selection. */
static const uint32_t station_ultrasound_threshold = 20000;

/*
 * JJY makes announcements during minutes 15 and 45. From about
 * [40.550-49.000) seconds, it transmits its callsign in Morse code.
 */
static const uint32_t station_jjy_morse_min = 15;
static const uint32_t station_jjy_morse_min2 = 45;
static const uint32_t station_jjy_morse_sec = 40;
static const uint32_t station_jjy_morse_ms = 550;
static const uint32_t station_jjy_morse_end_sec = 49;
static const uint32_t station_jjy_morse_tick =
    station_jjy_morse_sec * TSIG_STATION_TICKS_SEC +
    station_jjy_morse_ms / TSIG_STATION_MSECS_TICK;
static const uint32_t station_jjy_morse_end_tick =
    station_jjy_morse_end_sec * TSIG_STATION_TICKS_SEC;

/** Duration of Morse code symbols as ticks. */
static const uint32_t station_ticks_per_dit = 2;
static const uint32_t station_ticks_per_dah = 5;
static const uint32_t station_ticks_per_ieg = 1;  /* Inter-element gap. */
static const uint32_t station_ticks_per_icg = 6;  /* Inter-character gap. */
static const uint32_t station_ticks_per_iwg = 10; /* Inter-word gap. */

/** TTY inverse and reset. */
static const char *station_tty_inverse = "\x1b[7m";
static const char *station_tty_reset = "\x1b[0m";

/** Pointer to a function that updates state every minute. */
typedef void (*station_update_cb_t)(tsig_station_t *station,
                                    int64_t utc_timestamp);

/** Pointer to a function that logs status every second. */
typedef void (*station_status_cb_t)(tsig_station_t *station,
                                    int64_t utc_timestamp);

/** Functions that update state every minute. */
static void station_update_bpc(tsig_station_t *station, int64_t utc_timestamp);
static void station_update_dcf77(tsig_station_t *station,
                                 int64_t utc_timestamp);
static void station_update_jjy(tsig_station_t *station, int64_t utc_timestamp);
static void station_update_msf(tsig_station_t *station, int64_t utc_timestamp);
static void station_update_wwvb(tsig_station_t *station, int64_t utc_timestamp);

/** Functions that log status every second. */
static void station_status_bpc(tsig_station_t *station, int64_t utc_timestamp);
static void station_status_dcf77(tsig_station_t *station,
                                 int64_t utc_timestamp);
static void station_status_jjy(tsig_station_t *station, int64_t utc_timestamp);
static void station_status_msf(tsig_station_t *station, int64_t utc_timestamp);
static void station_status_wwvb(tsig_station_t *station, int64_t utc_timestamp);

/** Characteristics of a real time station's signal, etc. */
typedef struct station_info {
  station_update_cb_t update_cb; /** Per-minute state update callback. */
  station_status_cb_t status_cb; /** Per-second status logging callback. */
  int32_t utc_offset;            /** Usual (not summer time) UTC offset. */
  int32_t utc_st_offset;         /** Summer time UTC offset. */
  uint32_t freq;                 /** Actual broadcast frequency. */
  double xmit_low;               /** Low gain in [0.0-1.0]. */
  char *xmit_template;           /** Template for bit readout string. */
  char *xmit_sections;           /** Bit readout sections after expansion. */
  uint8_t xmit_bounds[8];        /** Bit readout section bounds. */
} station_info_t;

static station_info_t station_info[] = {
    /* clang-format off */
    [TSIG_STATION_ID_BPC] =
        {
            .update_cb = station_update_bpc,
            .status_cb = station_status_bpc,
            .utc_offset = 28800000, /* CST is UTC+0800 */
            .utc_st_offset = 28800000, /* still CST */
            .freq = 68500,
            .xmit_low = 3.162277660168379411765e-01, /* -10 dB */
            .xmit_template = "MM00XX0000000000X00000X00000000000000000",
            .xmit_sections = "secs hour   minute dow  pm dom    mon  year",
            .xmit_bounds = {4, 10, 16, 20, 22, 28, 32},
        },
    [TSIG_STATION_ID_DCF77] =
        {
            .update_cb = station_update_dcf77,
            .status_cb = station_status_dcf77,
            .utc_offset = 3600000, /* CET is UTC+0100 */
            .utc_st_offset = 7200000, /* CEST is UTC+0200 */
            .freq = 77500,
            .xmit_low = 1.496235656094433430496e-01, /* -16.5 dB */
            .xmit_template = "XXXXXXXXXXXXXXX00000X00000000000000000000000000000000000000M",
            .xmit_sections = "civil warning   flags minute    hour    dom    dow month year",
            .xmit_bounds = {15, 20, 29, 36, 42, 45, 50},

        },
    [TSIG_STATION_ID_JJY] =
        {
            .update_cb = station_update_jjy,
            .status_cb = station_status_jjy,
            .utc_offset = 32400000, /* JST is UTC+0900 */
            .utc_st_offset = 32400000, /* still JST */
            .freq = 40000,
            .xmit_low = 3.162277660168379411765e-01, /* -10 dB */
            .xmit_template = "M000X0000MXX00X0000MXX00X0000M0000XX00XMX00000000M00000XXXXM",
            .xmit_sections = "minute    hour       day of year     parity  year     dow  leapsec",
            .xmit_bounds = {9, 19, 34, 41, 49, 53},
        },
    [TSIG_STATION_ID_JJY60] =
        {
            .update_cb = station_update_jjy,
            .status_cb = station_status_jjy,
            .utc_offset = 32400000, /* JST is UTC+0900 */
            .utc_st_offset = 32400000, /* still JST */
            .freq = 60000,
            .xmit_low = 3.162277660168379411765e-01, /* -10 dB */
            .xmit_template = "M000X0000MXX00X0000MXX00X0000M0000XX00XMX00000000M00000XXXXM",
            .xmit_sections = "minute    hour       day of year     parity  year     dow  leapsec",
            .xmit_bounds = {9, 19, 34, 41, 49, 53},
        },
    [TSIG_STATION_ID_MSF] =
        {
            .update_cb = station_update_msf,
            .status_cb = station_status_msf,
            .utc_offset = 0, /* GMT is UTC+0000 */
            .utc_st_offset = 3600000, /* BST is UTC+0100 */
            .freq = 60000,
            .xmit_low = 0.0, /* On-off keying */
            .xmit_template = "M000000000000000000000000000000000000000000000000000X000000X",
            .xmit_sections = "dut1              year     month dom    dow hour   minute  minmark",
            .xmit_bounds = {17, 25, 30, 36, 39, 45, 52},
        },
    [TSIG_STATION_ID_WWVB] =
        {
            .update_cb = station_update_wwvb,
            .status_cb = station_status_wwvb,
            .utc_offset = 0, /* UTC */
            .utc_st_offset = 0, /* still UTC */
            .freq = 60000,
            .xmit_low = 1.412537544622754492885e-01, /* -17 dB */
            .xmit_template = "M000X0000MXX00X0000MXX00X0000M0000XX000M0000X0000M0000X0000M",
            .xmit_sections = "minute    hour       day of year     dut1       year       flags",
            .xmit_bounds = {9, 19, 34, 44, 54},
        },
    /* clang-format on */
};

/** Recognized time stations. */
static const tsig_mapping_t station_ids[] = {
    {"BPC", TSIG_STATION_ID_BPC},     {"DCF77", TSIG_STATION_ID_DCF77},
    {"JJY", TSIG_STATION_ID_JJY},     {"JJY40", TSIG_STATION_ID_JJY},
    {"JJY60", TSIG_STATION_ID_JJY60}, {"MSF", TSIG_STATION_ID_MSF},
    {"WWVB", TSIG_STATION_ID_WWVB},   {NULL, 0},
};

/** Perform linear interpolation between two doubles. */
static double station_lerp(double target_gain, double gain) {
  double diff = target_gain > gain ? target_gain - gain : gain - target_gain;
  return diff > station_lerp_min_delta ? (1.0 - station_lerp_rate) * gain +
                                             station_lerp_rate * target_gain
                                       : target_gain;
}

/** Compute even parity over a memory region. */
static uint8_t station_even_parity(uint8_t data[], uint32_t lo, uint32_t hi) {
  uint8_t parity = 0;
  for (uint32_t i = lo; i < hi; i++)
    for (uint8_t byte = data[i]; byte; byte &= byte - 1)
      parity = !parity;
  return parity;
}

/** Compute odd parity over a memory region. */
static uint8_t station_odd_parity(uint8_t data[], uint32_t lo, uint32_t hi) {
  return !station_even_parity(data, lo, hi);
}

/** Per-minute state update callback for BPC. */
static void station_update_bpc(tsig_station_t *station, int64_t utc_timestamp) {
  station_info_t *info = &station_info[TSIG_STATION_ID_BPC];
  uint8_t bits[20] = {[0] = station_sync_marker};
  tsig_datetime_t datetime;

  datetime = tsig_datetime_parse_timestamp(utc_timestamp + info->utc_offset);

  uint8_t hour_12h = datetime.hour % 12;
  bits[3] = (hour_12h >> 2) & 0x3;
  bits[4] = hour_12h & 0x3;

  uint8_t min = datetime.min;
  bits[5] = (min >> 4) & 0x3;
  bits[6] = (min >> 2) & 0x3;
  bits[7] = min & 0x3;

  uint8_t dow = datetime.dow ? datetime.dow : 7;
  bits[8] = (dow >> 2) & 0x1;
  bits[9] = dow & 0x3;

  uint8_t is_pm = datetime.hour >= 12;
  bits[10] = (is_pm << 1) | station_even_parity(bits, 1, 10);

  uint8_t day = datetime.day;
  bits[11] = (day >> 4) & 0x1;
  bits[12] = (day >> 2) & 0x3;
  bits[13] = day & 0x3;

  uint8_t mon = datetime.mon;
  bits[14] = (mon >> 2) & 0x3;
  bits[15] = mon & 0x3;

  uint8_t year = datetime.year % 100;
  bits[16] = (year >> 4) & 0x3;
  bits[17] = (year >> 2) & 0x3;
  bits[18] = year & 0x3;
  bits[19] = ((year >> 5) & 0x2) | station_even_parity(bits, 11, 19);

  char *template = info->xmit_template;
  for (uint32_t i = 0, j = 0, k = 1; i < sizeof(bits); i++, j += 2, k += 2) {
    station->xmit[j] = template[j] == '0' && (bits[i] & 2) ? '1' : template[j];
    station->xmit[k] = template[k] == '0' && (bits[i] & 1) ? '1' : template[k];
  }

  /* clang-format off */
  sprintf(station->meaning,
          /* "%02hhu:%02hhu:00 %s, weekday %hhu, day %hhu of month %hhu of year %hhu" */
          /* e.g. "00:34:00 PM, weekday 4, day 31 of month 12 of year 99" */
          "%02" PRIu8 ":%02" PRIu8 ":00 %s, weekday %" PRIu8
          ", day %" PRIu8 " of month %" PRIu8 " of year %" PRIu8,
          hour_12h, min, is_pm ? "PM" : "AM", dow, day, mon, year);
  /* clang-format on */

  for (uint32_t p = 0, j = 0; p < 3; p++) {
    if (p)
      bits[1] = 1 << p;
    if (p == 1)
      bits[10] ^= 1;

    /* Marker: Low for 0 ms, 00: 100 ms, 01: 200 ms, 10: 300 ms, 11: 400 ms. */
    for (uint32_t i = 0; i < sizeof(bits); i++) {
      uint32_t lo_dsec = bits[i] == station_sync_marker ? 0 : bits[i] + 1;
      uint32_t lo = 100 * lo_dsec / TSIG_STATION_MSECS_TICK;
      uint32_t hi = TSIG_STATION_TICKS_SEC - lo;
      for (; lo; j++, lo--)
        station->xmit_level[j / CHAR_BIT] &= ~((1 << (j % CHAR_BIT)));
      for (; hi; j++, hi--)
        station->xmit_level[j / CHAR_BIT] |= 1 << (j % CHAR_BIT);
    }
  }
}

/** Per-minute state update callback for DCF77. */
static void station_update_dcf77(tsig_station_t *station,
                                 int64_t utc_timestamp) {
  tsig_datetime_t utc_datetime = tsig_datetime_parse_timestamp(utc_timestamp);
  station_info_t *info = &station_info[TSIG_STATION_ID_DCF77];
  uint8_t bits[60] = {[20] = 1, [59] = station_sync_marker};

  /* Transmitted time is the CET/CEST time at the next UTC minute. */
  int32_t in_mins;
  bool is_cest = tsig_datetime_is_eu_dst(utc_datetime, &in_mins);
  bool is_xmit_cest = is_cest ^ (in_mins == 1);
  bool is_chg = 1 <= in_mins && in_mins <= 60;

  bits[16] = is_chg;
  bits[17] = is_xmit_cest;
  bits[18] = !is_xmit_cest;

  uint32_t civil_offset = is_xmit_cest ? info->utc_st_offset : info->utc_offset;
  int64_t timestamp = utc_timestamp + civil_offset + station_msecs_min;
  tsig_datetime_t datetime = tsig_datetime_parse_timestamp(timestamp);

  bits[20] = 1;

  uint8_t min = datetime.min % 10;
  bits[21] = min & 1;
  bits[22] = min & 2;
  bits[23] = min & 4;
  bits[24] = min & 8;

  uint8_t min_10 = datetime.min / 10;
  bits[25] = min_10 & 1;
  bits[26] = min_10 & 2;
  bits[27] = min_10 & 4;

  bits[28] = station_even_parity(bits, 21, 28);

  uint8_t hour = datetime.hour % 10;
  bits[29] = hour & 1;
  bits[30] = hour & 2;
  bits[31] = hour & 4;
  bits[32] = hour & 8;

  uint8_t hour_10 = datetime.hour / 10;
  bits[33] = hour_10 & 1;
  bits[34] = hour_10 & 2;

  bits[35] = station_even_parity(bits, 29, 35);

  uint8_t day = datetime.day % 10;
  bits[36] = day & 1;
  bits[37] = day & 2;
  bits[38] = day & 4;
  bits[39] = day & 8;

  uint8_t day_10 = datetime.day / 10;
  bits[40] = day_10 & 1;
  bits[41] = day_10 & 2;

  uint8_t dow = datetime.dow ? datetime.dow : 7;
  bits[42] = dow & 1;
  bits[43] = dow & 2;
  bits[44] = dow & 4;

  uint8_t mon = datetime.mon % 10;
  bits[45] = mon & 1;
  bits[46] = mon & 2;
  bits[47] = mon & 4;
  bits[48] = mon & 8;

  uint8_t mon_10 = datetime.mon / 10;
  bits[49] = mon_10 & 1;

  uint8_t year = datetime.year % 10;
  bits[50] = year & 1;
  bits[51] = year & 2;
  bits[52] = year & 4;
  bits[53] = year & 8;

  uint8_t year_10 = (datetime.year % 100) / 10;
  bits[54] = year_10 & 1;
  bits[55] = year_10 & 2;
  bits[56] = year_10 & 4;
  bits[57] = year_10 & 8;

  bits[58] = station_even_parity(bits, 36, 58);

  char *template = info->xmit_template;
  for (uint32_t i = 0; i < sizeof(bits); i++)
    station->xmit[i] = template[i] == '0' && bits[i] ? '1' : template[i];

  const char *chg_tz = is_cest ? "CET" : "CEST";
  const char *tz = is_xmit_cest ? "CEST" : "CET";
  /* clang-format off */
  sprintf(station->meaning,
          /* "%02hhu:%02hhu %s, %s next min %s, weekday %hhu, day %hhu of month %hhu of year %hu" */
          /* e.g. "12:34 CET, CEST next min no, weekday 4, day 31 of month 12 of year 99" */
          "%02" PRIu8 ":%02" PRIu8 " %s, %s next min %s, weekday %" PRIu8
          ", day %" PRIu8 " of month %" PRIu8 " of year %" PRIu16,
          datetime.hour, datetime.min,
          tz, chg_tz, is_chg ? "yes" : "no", dow,
          datetime.day, datetime.mon, datetime.year % 100);
  /* clang-format on */

  /* Marker: Low for 0 ms, 0: 100 ms, 1: 200 ms. */
  for (uint32_t i = 0, j = 0; i < sizeof(bits); i++) {
    uint32_t lo_dsec = bits[i] == station_sync_marker ? 0 : !!bits[i] + 1;
    uint32_t lo = 100 * lo_dsec / TSIG_STATION_MSECS_TICK;
    uint32_t hi = TSIG_STATION_TICKS_SEC - lo;
    for (; lo; j++, lo--)
      station->xmit_level[j / CHAR_BIT] &= ~((1 << (j % CHAR_BIT)));
    for (; hi; j++, hi--)
      station->xmit_level[j / CHAR_BIT] |= 1 << (j % CHAR_BIT);
  }
}

/** Insert high transmit level flags for JJY/JJY60 callsign announcements. */
static void station_xmit_jjy_morse_pulse(uint8_t xmit_level[], uint32_t *k,
                                         uint32_t ticks) {
  for (uint32_t i = 0, j = *k; i < ticks; i++, j++)
    xmit_level[j / CHAR_BIT] |= 1 << (j % CHAR_BIT);
  *k += ticks;
}

/** Insert transmit level flags for JJY/JJY60 callsign announcements. */
static void station_xmit_jjy_morse(uint8_t xmit_level[]) {
  uint32_t lo = station_jjy_morse_sec * TSIG_STATION_TICKS_SEC;
  uint32_t hi = station_jjy_morse_end_sec * TSIG_STATION_TICKS_SEC;
  for (uint32_t i = lo; i < hi; i++)
    xmit_level[i / CHAR_BIT] &= ~((1 << (i % CHAR_BIT)));

  uint32_t k = station_jjy_morse_tick;
  for (uint32_t i = 0; i < 2; i++) {
    /* JJ, i.e. .--- .--- */
    for (uint32_t j = 0; j < 2; j++) {
      station_xmit_jjy_morse_pulse(xmit_level, &k, station_ticks_per_dit);
      k += station_ticks_per_ieg;
      station_xmit_jjy_morse_pulse(xmit_level, &k, station_ticks_per_dah);
      k += station_ticks_per_ieg;
      station_xmit_jjy_morse_pulse(xmit_level, &k, station_ticks_per_dah);
      k += station_ticks_per_ieg;
      station_xmit_jjy_morse_pulse(xmit_level, &k, station_ticks_per_dah);
      k += station_ticks_per_icg;
    }
    /* Y, i.e. -.-- */
    station_xmit_jjy_morse_pulse(xmit_level, &k, station_ticks_per_dah);
    k += station_ticks_per_ieg;
    station_xmit_jjy_morse_pulse(xmit_level, &k, station_ticks_per_dit);
    k += station_ticks_per_ieg;
    station_xmit_jjy_morse_pulse(xmit_level, &k, station_ticks_per_dah);
    k += station_ticks_per_ieg;
    station_xmit_jjy_morse_pulse(xmit_level, &k, station_ticks_per_dah);
    k += station_ticks_per_iwg;
  }
}

/** Per-minute state update callback for JJY and JJY60. */
static void station_update_jjy(tsig_station_t *station, int64_t utc_timestamp) {
  station_info_t *info = &station_info[TSIG_STATION_ID_JJY];
  uint8_t bits[60] = {
      [0] = station_sync_marker,  [9] = station_sync_marker,
      [19] = station_sync_marker, [29] = station_sync_marker,
      [39] = station_sync_marker, [49] = station_sync_marker,
      [59] = station_sync_marker,
  };
  tsig_datetime_t datetime;

  datetime = tsig_datetime_parse_timestamp(utc_timestamp + info->utc_offset);

  uint8_t min_10 = datetime.min / 10;
  bits[1] = min_10 & 4;
  bits[2] = min_10 & 2;
  bits[3] = min_10 & 1;

  uint8_t min = datetime.min % 10;
  bits[5] = min & 8;
  bits[6] = min & 4;
  bits[7] = min & 2;
  bits[8] = min & 1;

  uint8_t hour_10 = datetime.hour / 10;
  bits[12] = hour_10 & 2;
  bits[13] = hour_10 & 1;

  uint8_t hour = datetime.hour % 10;
  bits[15] = hour & 8;
  bits[16] = hour & 4;
  bits[17] = hour & 2;
  bits[18] = hour & 1;

  uint8_t doy_100 = datetime.doy / 100;
  bits[22] = doy_100 & 2;
  bits[23] = doy_100 & 1;

  uint8_t doy_10 = (datetime.doy % 100) / 10;
  bits[25] = doy_10 & 8;
  bits[26] = doy_10 & 4;
  bits[27] = doy_10 & 2;
  bits[28] = doy_10 & 1;

  uint8_t doy = datetime.doy % 10;
  bits[30] = doy & 8;
  bits[31] = doy & 4;
  bits[32] = doy & 2;
  bits[33] = doy & 1;

  bits[36] = station_even_parity(bits, 12, 19);
  bits[37] = station_even_parity(bits, 1, 9);

  uint8_t year_10 = (datetime.year % 100) / 10;
  bits[41] = year_10 & 8;
  bits[42] = year_10 & 4;
  bits[43] = year_10 & 2;
  bits[44] = year_10 & 1;

  uint8_t year = datetime.year % 10;
  bits[45] = year & 8;
  bits[46] = year & 4;
  bits[47] = year & 2;
  bits[48] = year & 1;

  uint8_t dow = datetime.dow;
  bits[50] = dow & 4;
  bits[51] = dow & 2;
  bits[52] = dow & 1;

  char *template = info->xmit_template;
  for (uint32_t i = 0; i < sizeof(bits); i++)
    station->xmit[i] = template[i] == '0' && bits[i] ? '1' : template[i];

  /* clang-format off */
  sprintf(station->meaning,
          /* "%02hhu:%02hhu, day %hu of year %hu, weekday %hhu, leapsec end mon +0" */
          /* e.g. "12:34, day 365 of year 99, weekday 4, leapsec end mon +0" */
          "%02" PRIu8 ":%02" PRIu8 ", day %" PRIu16 " of year %" PRIu16
          ", weekday %" PRIu8 ", leapsec end mon +0",
          datetime.hour, datetime.min, datetime.doy, datetime.year % 100, dow);
  /* clang-format on */

  bool is_announce = datetime.min == station_jjy_morse_min ||
                     datetime.min == station_jjy_morse_min2;
  if (is_announce) {
    bits[50] = 0;
    bits[51] = 0;
    bits[52] = 0;
  }

  /* Marker: High for 200 ms, 0: 800 ms, 1: 500 ms. */
  for (uint32_t i = 0, j = 0; i < sizeof(bits); i++) {
    if (is_announce && i == station_jjy_morse_sec) {
      station_xmit_jjy_morse(station->xmit_level);
      i = station_jjy_morse_end_sec;
      j = station_jjy_morse_end_tick;
    }

    uint32_t hi_dsec = bits[i] == station_sync_marker ? 2 : bits[i] ? 5 : 8;
    uint32_t hi = 100 * hi_dsec / TSIG_STATION_MSECS_TICK;
    uint32_t lo = TSIG_STATION_TICKS_SEC - hi;
    for (; hi; j++, hi--)
      station->xmit_level[j / CHAR_BIT] |= 1 << (j % CHAR_BIT);
    for (; lo; j++, lo--)
      station->xmit_level[j / CHAR_BIT] &= ~((1 << (j % CHAR_BIT)));
  }
}

/** Per-minute state update callback for MSF. */
static void station_update_msf(tsig_station_t *station, int64_t utc_timestamp) {
  tsig_datetime_t utc_datetime = tsig_datetime_parse_timestamp(utc_timestamp);
  station_info_t *info = &station_info[TSIG_STATION_ID_MSF];
  uint8_t bits[60] = {[0] = station_sync_marker};

  int16_t dut1 = station->dut1 / 100;
  uint8_t lt0 = dut1 < 0 ? 8 : 0;
  if (lt0)
    dut1 = -dut1;
  bits[1 + lt0] = dut1 >= 1;
  bits[2 + lt0] = dut1 >= 2;
  bits[3 + lt0] = dut1 >= 3;
  bits[4 + lt0] = dut1 >= 4;
  bits[5 + lt0] = dut1 >= 5;
  bits[6 + lt0] = dut1 >= 6;
  bits[7 + lt0] = dut1 >= 7;
  bits[8 + lt0] = dut1 >= 8;

  int32_t in_mins;
  bool is_bst = tsig_datetime_is_eu_dst(utc_datetime, &in_mins);
  bool is_chg = 1 <= in_mins && in_mins <= 61;

  /* Transmitted time is the GMT/BST time at the next UTC minute. */
  bool is_xmit_bst = is_bst ^ (in_mins == 1);
  uint32_t civil_offset = is_xmit_bst ? info->utc_st_offset : info->utc_offset;
  int64_t timestamp = utc_timestamp + civil_offset + station_msecs_min;
  tsig_datetime_t datetime = tsig_datetime_parse_timestamp(timestamp);

  uint8_t year_10 = (datetime.year % 100) / 10;
  bits[17] = year_10 & 8;
  bits[18] = year_10 & 4;
  bits[19] = year_10 & 2;
  bits[20] = year_10 & 1;

  uint8_t year = datetime.year % 10;
  bits[21] = year & 8;
  bits[22] = year & 4;
  bits[23] = year & 2;
  bits[24] = year & 1;

  uint8_t mon_10 = datetime.mon / 10;
  bits[25] = mon_10 & 1;

  uint8_t mon = datetime.mon % 10;
  bits[26] = mon & 8;
  bits[27] = mon & 4;
  bits[28] = mon & 2;
  bits[29] = mon & 1;

  uint8_t day_10 = datetime.day / 10;
  bits[30] = day_10 & 2;
  bits[31] = day_10 & 1;

  uint8_t day = datetime.day % 10;
  bits[32] = day & 8;
  bits[33] = day & 4;
  bits[34] = day & 2;
  bits[35] = day & 1;

  uint8_t dow = datetime.dow;
  bits[36] = dow & 4;
  bits[37] = dow & 2;
  bits[38] = dow & 1;

  uint8_t hour_10 = datetime.hour / 10;
  bits[39] = hour_10 & 2;
  bits[40] = hour_10 & 1;

  uint8_t hour = datetime.hour % 10;
  bits[41] = hour & 8;
  bits[42] = hour & 4;
  bits[43] = hour & 2;
  bits[44] = hour & 1;

  uint8_t min_10 = datetime.min / 10;
  bits[45] = min_10 & 4;
  bits[46] = min_10 & 2;
  bits[47] = min_10 & 1;

  uint8_t min = datetime.min % 10;
  bits[48] = min & 8;
  bits[49] = min & 4;
  bits[50] = min & 2;
  bits[51] = min & 1;

  bits[53] = is_chg;
  bits[54] = station_odd_parity(bits, 17, 25);
  bits[55] = station_odd_parity(bits, 25, 36);
  bits[56] = station_odd_parity(bits, 36, 39);
  bits[57] = station_odd_parity(bits, 39, 52);
  bits[58] = is_xmit_bst;

  char *template = info->xmit_template;
  for (uint32_t i = 0; i < sizeof(bits); i++)
    station->xmit[i] = template[i] == '0' && bits[i] ? '1' : template[i];

  const char *chg_tz = is_bst ? "GMT" : "BST";
  const char *tz = is_xmit_bst ? "BST" : "GMT";
  const char *chg = is_chg ? "yes" : "no";
  /* clang-format off */
  sprintf(station->meaning,
          /* "DUT1 %s0.%hd, d%hhu of m%hhu of y%hhu, weekday %hhu, %02hhu:%02hhu %s, %s next hour %s" */
          /* e.g. "DUT1 +0.0, d31 of m12 of y99, weekday 4, 12:34 GMT, BST next hour no" */
          "DUT1 %s0.%" PRIi16 ", d%" PRIu8 " of m%" PRIu8 " of y%" PRIu16
          ", weekday %" PRIu8 ", %02" PRIu8 ":%02" PRIu8 " %s, %s next hour %s",
          lt0 ? "-" : "+", dut1,
          datetime.day, datetime.mon, datetime.year % 100,
          dow, datetime.hour, datetime.min, tz, chg_tz, chg);
  /* clang-format on */

  /*
   * Marker: Low for 500 ms, 00: 100 ms, 01: 200 ms, 11: 300 ms.
   * Note that 11 can only occur during the secondary minute marker.
   */
  for (uint32_t i = 0, j = 0; i < sizeof(bits); i++) {
    uint32_t dsec_lo = bits[i] == station_sync_marker ? 5 : !!bits[i] + 1;
    dsec_lo += 53 <= i && i <= 58; /* Secondary 01111110 minute marker. */
    uint32_t lo = 100 * dsec_lo / TSIG_STATION_MSECS_TICK;
    uint32_t hi = TSIG_STATION_TICKS_SEC - lo;
    for (; lo; j++, lo--)
      station->xmit_level[j / CHAR_BIT] &= ~((1 << (j % CHAR_BIT)));
    for (; hi; j++, hi--)
      station->xmit_level[j / CHAR_BIT] |= 1 << (j % CHAR_BIT);
  }
}

/** Per-minute state update callback for WWVB. */
static void station_update_wwvb(tsig_station_t *station,
                                int64_t utc_timestamp) {
  tsig_datetime_t utc_datetime = tsig_datetime_parse_timestamp(utc_timestamp);
  station_info_t *info = &station_info[TSIG_STATION_ID_WWVB];
  tsig_datetime_t datetime =
      tsig_datetime_parse_timestamp(utc_timestamp + info->utc_offset);
  uint8_t bits[60] = {
      [0] = station_sync_marker,  [9] = station_sync_marker,
      [19] = station_sync_marker, [29] = station_sync_marker,
      [39] = station_sync_marker, [49] = station_sync_marker,
      [59] = station_sync_marker,
  };

  uint8_t min_10 = datetime.min / 10;
  bits[1] = min_10 & 4;
  bits[2] = min_10 & 2;
  bits[3] = min_10 & 1;

  uint8_t min = datetime.min % 10;
  bits[5] = min & 8;
  bits[6] = min & 4;
  bits[7] = min & 2;
  bits[8] = min & 1;

  uint8_t hour_10 = datetime.hour / 10;
  bits[12] = hour_10 & 2;
  bits[13] = hour_10 & 1;

  uint8_t hour = datetime.hour % 10;
  bits[15] = hour & 8;
  bits[16] = hour & 4;
  bits[17] = hour & 2;
  bits[18] = hour & 1;

  uint8_t doy_100 = datetime.doy / 100;
  bits[22] = doy_100 & 2;
  bits[23] = doy_100 & 1;

  uint8_t doy_10 = (datetime.doy % 100) / 10;
  bits[25] = doy_10 & 8;
  bits[26] = doy_10 & 4;
  bits[27] = doy_10 & 2;
  bits[28] = doy_10 & 1;

  uint8_t doy = datetime.doy % 10;
  bits[30] = doy & 8;
  bits[31] = doy & 4;
  bits[32] = doy & 2;
  bits[33] = doy & 1;

  int16_t dut1 = station->dut1 / 100;
  bool lt0 = dut1 < 0;
  bits[36] = dut1 >= 0;
  bits[37] = dut1 < 0;
  bits[38] = dut1 >= 0;
  if (lt0)
    dut1 = -dut1;
  bits[40] = dut1 & 8;
  bits[41] = dut1 & 4;
  bits[42] = dut1 & 2;
  bits[43] = dut1 & 1;

  uint8_t year_10 = (datetime.year % 100) / 10;
  bits[45] = year_10 & 8;
  bits[46] = year_10 & 4;
  bits[47] = year_10 & 2;
  bits[48] = year_10 & 1;

  uint8_t year = datetime.year % 10;
  bits[50] = year & 8;
  bits[51] = year & 4;
  bits[52] = year & 2;
  bits[53] = year & 1;

  bool is_leap = tsig_datetime_is_leap(datetime.year);
  bits[55] = is_leap;

  bool is_dst_end;
  bool is_dst = tsig_datetime_is_us_dst(utc_datetime, &is_dst_end);
  bits[57] = is_dst_end;
  bits[58] = is_dst;

  char *template = info->xmit_template;
  for (uint32_t i = 0; i < sizeof(bits); i++)
    station->xmit[i] = template[i] == '0' && bits[i] ? '1' : template[i];

  /* clang-format off */
  sprintf(station->meaning,
          /* "%02hhu:%02hhu, day %hu of year %hu, DUT1 %s0.%hu, leap year %s, DST %s" */
          /* e.g. "12:34, day 365 of year 99, DUT1 +0.0, leap year no, DST no" */
          "%02" PRIu8 ":%02" PRIu8 ", day %" PRIu16 " of year %" PRIu16
          ", DUT1 %s0.%" PRIi16 ", leap year %s, DST %s",
          datetime.hour, datetime.min, datetime.doy, datetime.year % 100,
          lt0 ? "-" : "+", dut1, is_leap ? "yes" : "no",
          is_dst && is_dst_end    ? "yes"
          : !is_dst && is_dst_end ? "begins today"
          : is_dst && !is_dst_end ? "ends today"
                                  : "no");
  /* clang-format on */

  /* Marker: Low for 800 ms, 0: 200 ms, 1: 500 ms. */
  for (uint32_t i = 0, j = 0; i < sizeof(bits); i++) {
    uint32_t dsec_lo = bits[i] == station_sync_marker ? 8 : bits[i] ? 5 : 2;
    uint32_t lo = 100 * dsec_lo / TSIG_STATION_MSECS_TICK;
    uint32_t hi = TSIG_STATION_TICKS_SEC - lo;
    for (; lo; j++, lo--)
      station->xmit_level[j / CHAR_BIT] &= ~((1 << (j % CHAR_BIT)));
    for (; hi; j++, hi--)
      station->xmit_level[j / CHAR_BIT] |= 1 << (j % CHAR_BIT);
  }
}

/** Write bit readout to a buffer with highlighting and spacing.  */
static void station_status_write_xmit_readout(char buf[], uint8_t sec,
                                              char xmit[],
                                              uint8_t xmit_bounds[]) {
  uint8_t *bounds = xmit_bounds;
  char *wr = buf;
  for (uint8_t i = 0; i < 60; i++) {
    if (i == *bounds) {
      *wr++ = ' ';
      bounds++;
    }
    if (i == sec)
      wr += sprintf(wr, "%s", station_tty_inverse);
    *wr++ = xmit[i];
    if (i == sec)
      wr += sprintf(wr, "%s", station_tty_reset);
  }
  *wr = '\0';
}

/** Per-second status logging callback for BPC. */
static void station_status_bpc(tsig_station_t *station, int64_t utc_timestamp) {
  station_info_t *info = &station_info[TSIG_STATION_ID_BPC];
  tsig_datetime_t datetime =
      tsig_datetime_parse_timestamp(utc_timestamp + info->utc_offset);
  char buf[TSIG_STATION_MESSAGE_SIZE];
  char cur[TSIG_STATION_MESSAGE_SIZE];
  char *meaning = station->meaning;
  tsig_log_t *log = station->log;
  char *xmit = station->xmit;
  uint8_t sec = datetime.sec;
  uint8_t xi = (2 * sec) % 40;
  uint8_t xj = xi + 1;

  /* Fake updates to xmit and meaning at 20 and 40 seconds. */
  if (sec == 20) {
    meaning[6] = '2';
    xmit[3] = '1';
    xmit[21] = xmit[21] == '0' ? '1' : '0';
  } else if (sec == 40) {
    meaning[6] = '4';
    xmit[2] = '1';
    xmit[3] = '0';
  }

  /* clang-format off */
  sprintf(buf,
          /* "%04hu-%02hhu-%02hhu %02hhu:%02hhu:%02hhu CST" */
          /* e.g. "2099-12-31 12:34:00 CST" */
          "%04" PRIu16 "-%02" PRIu8 "-%02" PRIu8
          " %02" PRIu8 ":%02" PRIu8 ":%02" PRIu8 " CST",
          datetime.year, datetime.mon, datetime.day,
          datetime.hour, datetime.min, datetime.sec);
  /* clang-format on */

  /* e.g. "BPC     2099-12-31 12:34:00 CST, transmitting marker" */
  const char *inverse = station->verbose ? station_tty_inverse : "";
  const char *reset = station->verbose ? station_tty_reset : "";
  if (!xi)
    sprintf(cur, "marker");
  else if (xi == 4)
    sprintf(cur, "00");
  else if (xi == 16 || xi == 22)
    sprintf(cur, "0%s%c%s", inverse, xmit[xj], reset);
  else
    sprintf(cur, "%s%c%c%s", inverse, xmit[xi], xmit[xj], reset);
  tsig_log_status(1, "BPC     %s, transmitting %s", buf, cur);

  if (!station->verbose)
    return;

  /* e.g. "meaning 00:34:00 PM, weekday 4, day 31 of month 12 of year 99" */
  tsig_log_status(2, "meaning %s", meaning);

  /* e.g. "MM00 XX0000 000000 X000 00 X00000 0000 00000000" */
  uint8_t *bounds = info->xmit_bounds;
  char *wr = buf;
  for (uint8_t i = 0, j = 1; i < 40; i += 2, j += 2) {
    if (i == *bounds) {
      *wr++ = ' ';
      bounds++;
    }
    if (i == xi)
      wr += sprintf(wr, "%s", station_tty_inverse);
    *wr++ = xmit[i];
    *wr++ = xmit[j];
    if (i == xi)
      wr += sprintf(wr, "%s", station_tty_reset);
  }
  *wr = '\0';

  /* e.g. "   bits MM00 XX0000 000000 X000 00 X00000 0000 00000000" */
  tsig_log_status(3, "   bits %s", buf);

  /* e.g. "        secs hour   minute dow  pm dom    mon  year" */
  tsig_log_status(4, "        %s", info->xmit_sections);
}

/** Per-second status logging callback for DCF77. */
static void station_status_dcf77(tsig_station_t *station,
                                 int64_t utc_timestamp) {
  tsig_datetime_t utc_datetime = tsig_datetime_parse_timestamp(utc_timestamp);
  station_info_t *info = &station_info[TSIG_STATION_ID_DCF77];
  char buf[TSIG_STATION_MESSAGE_SIZE];
  char cur[TSIG_STATION_MESSAGE_SIZE];
  tsig_log_t *log = station->log;
  char *xmit = station->xmit;
  tsig_datetime_t datetime;
  uint8_t sec;

  /* We only care about whether it's currently CET/CEST. */
  bool is_cest = tsig_datetime_is_eu_dst(utc_datetime, NULL);
  uint32_t cest_offset = is_cest ? station_msecs_hour : 0;
  uint32_t utc_offset = info->utc_offset + cest_offset;
  datetime = tsig_datetime_parse_timestamp(utc_timestamp + utc_offset);
  sec = datetime.sec;

  /* clang-format off */
  sprintf(buf,
          /* "%04hu-%02hhu-%02hhu %02hhu:%02hhu:%02hhu %s" */
          /* e.g. "2099-12-31 12:34:56 CET" */
          "%04" PRIu16 "-%02" PRIu8 "-%02" PRIu8
          " %02" PRIu8 ":%02" PRIu8 ":%02" PRIu8 " %s",
          datetime.year, datetime.mon, datetime.day,
          datetime.hour, datetime.min, datetime.sec,
          is_cest ? "CEST" : "CET");
  /* clang-format on */

  /* e.g. "DCF77   2099-12-31 12:34:56 CET, transmitting marker" */
  const char *inverse = station->verbose ? station_tty_inverse : "";
  const char *reset = station->verbose ? station_tty_reset : "";
  if (xmit[sec] == 'M')
    sprintf(cur, "marker");
  else if (xmit[sec] == 'X')
    sprintf(cur, sec == 20 ? "1" : "0");
  else
    sprintf(cur, "%s%c%s", inverse, xmit[sec], reset);
  tsig_log_status(1, "DCF77   %s, transmitting %s", buf, cur);

  if (!station->verbose)
    return;

  /* e.g. "meaning 12:34 CET, CEST next min no, weekday 4, day 31 of month 12 of year 99" */
  tsig_log_status(2, "meaning %s", station->meaning);

  /* e.g. "   bits XXXXXXXXXXXXXXX 00000 X00000000 0000000 000000 000 00000 000000000M" */
  station_status_write_xmit_readout(buf, sec, xmit, info->xmit_bounds);
  tsig_log_status(3, "   bits %s", buf);

  /* e.g. "        civil warning   flags minute    hour    dom    dow month year" */
  tsig_log_status(4, "        %s", info->xmit_sections);
}

/** Per-second status logging callback for JJY. */
static void station_status_jjy(tsig_station_t *station, int64_t utc_timestamp) {
  station_info_t *info = &station_info[TSIG_STATION_ID_JJY];
  char buf[TSIG_STATION_MESSAGE_SIZE];
  char cur[TSIG_STATION_MESSAGE_SIZE];
  tsig_log_t *log = station->log;
  char *xmit = station->xmit;
  tsig_datetime_t datetime;
  uint8_t sec;

  datetime = tsig_datetime_parse_timestamp(utc_timestamp + info->utc_offset);
  sec = datetime.sec;

  /* clang-format off */
  sprintf(buf,
          /* "%04hu-%02hhu-%02hhu %02hhu:%02hhu:%02hhu JST" */
          /* e.g. "2099-12-31 12:34:56 JST" */
          "%04" PRIu16 "-%02" PRIu8 "-%02" PRIu8
          " %02" PRIu8 ":%02" PRIu8 ":%02" PRIu8 " JST",
          datetime.year, datetime.mon, datetime.day,
          datetime.hour, datetime.min, datetime.sec);
  /* clang-format on */

  /* e.g. "JJY60   2099-12-31 12:34:56 JST, transmitting marker" */
  const char *inverse = station->verbose ? station_tty_inverse : "";
  const char *reset = station->verbose ? station_tty_reset : "";
  bool is_jjy60 = station->station == TSIG_STATION_ID_JJY60;
  const char *callsign = is_jjy60 ? "JJY60" : "JJY";
  if (xmit[sec] == 'M')
    sprintf(cur, "marker");
  else if (xmit[sec] == 'X')
    sprintf(cur, "0");
  else
    sprintf(cur, "%s%c%s", inverse, xmit[sec], reset);
  tsig_log_status(1, "%-8s%s, transmitting %s", callsign, buf, cur);

  if (!station->verbose)
    return;

  /* e.g. "meaning 12:34, day 365 of year 99, weekday 4, leapsec end mon +0" */
  tsig_log_status(2, "meaning %s", station->meaning);

  /* e.g. "   bits M000X0000 MXX00X0000 MXX00X0000M0000 XX00XMX 00000000 M000 00XXXXM" */
  station_status_write_xmit_readout(buf, sec, xmit, info->xmit_bounds);
  tsig_log_status(3, "   bits %s", buf);

  /* e.g. "        minute    hour       day of year     parity  year     dow  leapsec" */
  tsig_log_status(4, "        %s", info->xmit_sections);
}

/** Per-second status logging callback for MSF. */
static void station_status_msf(tsig_station_t *station, int64_t utc_timestamp) {
  tsig_datetime_t utc_datetime = tsig_datetime_parse_timestamp(utc_timestamp);
  station_info_t *info = &station_info[TSIG_STATION_ID_MSF];
  char buf[TSIG_STATION_MESSAGE_SIZE];
  char cur[TSIG_STATION_MESSAGE_SIZE];
  tsig_log_t *log = station->log;
  char *xmit = station->xmit;
  tsig_datetime_t datetime;
  uint8_t sec;

  /* We only care about whether it's currently GMT/BST. */
  bool is_bst = tsig_datetime_is_eu_dst(utc_datetime, NULL);
  uint32_t bst_offset = is_bst ? station_msecs_hour : 0;
  uint32_t utc_offset = info->utc_offset + bst_offset;
  datetime = tsig_datetime_parse_timestamp(utc_timestamp + utc_offset);
  sec = datetime.sec;

  /* clang-format off */
  sprintf(buf,
          /* "%04hu-%02hhu-%02hhu %02hhu:%02hhu:%02hhu %s" */
          /* e.g. "2099-12-31 12:34:56 GMT" */
          "%04" PRIu16 "-%02" PRIu8 "-%02" PRIu8
          " %02" PRIu8 ":%02" PRIu8 ":%02" PRIu8 " %s",
          datetime.year, datetime.mon, datetime.day,
          datetime.hour, datetime.min, datetime.sec,
          is_bst ? "BST" : "GMT");
  /* clang-format on */

  /* e.g. "MSF     2099-12-31 12:34:56 GMT, transmitting marker" */
  if (!sec)
    sprintf(cur, "marker");
  else if (sec != 52 && sec != 59 && station->verbose)
    sprintf(cur,
            sec <= 16   ? "0%s%c%s"
            : sec <= 51 ? "%s%c%s0"
                        : "1%s%c%s", /* 53 <= sec && sec <= 58 */
            station_tty_inverse, xmit[sec], station_tty_reset);
  else if (sec != 52 && sec != 59)
    sprintf(cur,
            sec <= 16   ? "0%c"
            : sec <= 51 ? "%c0"
                        : "1%c", /* 53 <= sec && sec <= 58 */
            xmit[sec]);
  else /* sec == 52 || sec == 59 */
    sprintf(cur, "00");
  tsig_log_status(1, "MSF     %s, transmitting %s", buf, cur);

  if (!station->verbose)
    return;

  /* e.g. "meaning DUT1 +0.0, d31 of m12 of y99, weekday 4, 12:34 GMT, BST next hour no" */
  tsig_log_status(2, "meaning %s", station->meaning);

  /* e.g. "   bits M0000000000000000 00000000 00000 000000 000 000000 0000000 X000000X" */
  station_status_write_xmit_readout(buf, sec, xmit, info->xmit_bounds);
  tsig_log_status(3, "   bits %s", buf);

  /* e.g. "        dut1              year     month dom    dow hour   minute  minmark" */
  tsig_log_status(4, "        %s", info->xmit_sections);
}

/** Per-second status logging callback for WWVB. */
static void station_status_wwvb(tsig_station_t *station,
                                int64_t utc_timestamp) {
  station_info_t *info = &station_info[TSIG_STATION_ID_WWVB];
  char buf[TSIG_STATION_MESSAGE_SIZE];
  char cur[TSIG_STATION_MESSAGE_SIZE];
  tsig_log_t *log = station->log;
  char *xmit = station->xmit;
  tsig_datetime_t datetime;
  uint8_t sec;

  datetime = tsig_datetime_parse_timestamp(utc_timestamp + info->utc_offset);
  sec = datetime.sec;

  /* clang-format off */
  sprintf(buf,
          /* "%04hu-%02hhu-%02hhu %02hhu:%02hhu:%02hhu UTC" */
          /* e.g. "2099-12-31 12:34:56 UTC" */
          "%04" PRIu16 "-%02" PRIu8 "-%02" PRIu8
          " %02" PRIu8 ":%02" PRIu8 ":%02" PRIu8 " UTC",
          datetime.year, datetime.mon, datetime.day,
          datetime.hour, datetime.min, datetime.sec);
  /* clang-format on */

  /* e.g. "WWVB    2099-12-31 12:34:56 UTC, transmitting marker" */
  const char *inverse = station->verbose ? station_tty_inverse : "";
  const char *reset = station->verbose ? station_tty_reset : "";
  if (xmit[sec] == 'M')
    sprintf(cur, "marker");
  else if (xmit[sec] == 'X')
    sprintf(cur, "0");
  else
    sprintf(cur, "%s%c%s", inverse, xmit[sec], reset);
  tsig_log_status(1, "WWVB    %s, transmitting %s", buf, cur);

  if (!station->verbose)
    return;

  /* e.g. "meaning 12:34, day 365 of year 99, DUT1 +0.0, leap year no, DST no" */
  tsig_log_status(2, "meaning %s", station->meaning);

  /* e.g. "   bits M000X0000 MXX00X0000 MXX00X0000M0000 XX000M0000 X0000M0000 X0000M" */
  station_status_write_xmit_readout(buf, sec, xmit, info->xmit_bounds);
  tsig_log_status(3, "   bits %s", buf);

  /* e.g. "        minute    hour       day of year     dut1       year       flags" */
  tsig_log_status(4, "        %s", info->xmit_sections);
}

/** Print station information. */
static void station_init_print(tsig_log_t *log, tsig_station_id_t station_id,
                               int64_t base, int32_t offset, int16_t dut1,
                               bool smooth, bool ultrasound, uint32_t freq,
                               uint32_t subharmonic) {
  const char *sign = offset < 0 ? "-" : "";
  int32_t coeff = offset < 0 ? -1 : 1;
  char msg[TSIG_STATION_MESSAGE_SIZE];
  tsig_datetime_t base_datetime;
  tsig_datetime_t datetime;
  int len;

  datetime = tsig_datetime_parse_timestamp(coeff * offset);

  /* clang-format off */
  len = sprintf(msg, "Starting %s", tsig_station_name(station_id));
  if (base >= 0) {
    base_datetime = tsig_datetime_parse_timestamp(base);
    len += sprintf(&msg[len], /* " from %04hu-%02hhu-%02hhu %02hhu:%02hhu:%02hhu" */
                   " from %04" PRIu16 "-%02" PRIu8 "-%02" PRIu8
                   " %02" PRIu8 ":%02" PRIu8 ":%02" PRIu8,
                   base_datetime.year, base_datetime.mon, base_datetime.day,
                   base_datetime.hour, base_datetime.min, base_datetime.sec);
  }
  len += sprintf(&msg[len], /* " adjusted by %s%02hhu:%02hhu:%02hhu.%03hu" */
                 " adjusted by %s%02" PRIu8 ":%02" PRIu8 ":%02" PRIu8 ".%03" PRIu16,
                 sign, datetime.hour, datetime.min, datetime.sec, datetime.msec);
  if (station_id == TSIG_STATION_ID_MSF || station_id == TSIG_STATION_ID_WWVB)
    len += sprintf(&msg[len], ", DUT1 %" PRIi16 " ms", dut1);
  tsig_log("%s.", msg);

  tsig_log_dbg("Gain smoothing %s, ultrasound output %sallowed.",
               smooth ? "on" : "off", ultrasound ? "" : "not ");

  tsig_log_dbg("Generating %" PRIu32 " Hz carrier"
               " (subharmonic %" PRIu32 " of %" PRIu32 " Hz).",
               freq / subharmonic, subharmonic, freq);
  /* clang-format on */
}

#ifdef TSIG_DEBUG
/** Print transmit level flags. */
void station_xmit_level_print(tsig_log_t *log, uint8_t xmit_level[]) {
  char msg[TSIG_STATION_MESSAGE_SIZE];
  uint8_t bit = 0x01;
  uint8_t i = 0;
  for (uint8_t line = 0; line < 10; line++) {
    char *wr = msg;
    for (uint8_t sec = 0; sec < 6; sec++) {
      if (sec)
        *wr++ = ' ';
      for (uint8_t dsec = 0; dsec < 10; dsec++) {
        uint8_t b0 = xmit_level[i] & bit;
        bit = (bit << 1) | (bit >> 7);
        i += bit == 0x01;
        uint8_t b1 = xmit_level[i] & bit;
        bit = (bit << 1) | (bit >> 7);
        i += bit == 0x01;
        *wr++ = (!b0 && !b1)  ? '.'
                : (!b0 && b1) ? '/'
                : (b0 && b1)  ? '|'
                              : '\\';
      }
    }
    *wr++ = '\0';
    tsig_log_dbg("    %s", msg);
  }
}

/** Print initialized station context. */
void station_print(tsig_station_t *station) {
  const char *station_name = tsig_station_name(station->station);
  tsig_log_t *log = station->log;
  tsig_log_dbg("tsig_station_t %p = {", station);
  tsig_log_dbg("  .station        = %s,", station_name);
  tsig_log_dbg("  .base           = %" PRIi64 ",", station->base);
  tsig_log_dbg("  .offset         = %" PRIi32 ",", station->offset);
  tsig_log_dbg("  .dut1           = %" PRIi16 ",", station->dut1);
  tsig_log_dbg("  .smooth         = %d,", station->smooth);
  tsig_log_dbg("  .rate           = %" PRIu32 ",", station->rate);
  tsig_log_dbg("  .xmit_level     = {");
  station_xmit_level_print(log, station->xmit_level);
  tsig_log_dbg("  },");
  tsig_log_dbg("  .xmit           = %p,", station->xmit);
  tsig_log_dbg("  .meaning        = %p,", station->meaning);
  tsig_log_dbg("  .base_offset    = %" PRIi64 ",", station->base_offset);
  tsig_log_dbg("  .timestamp      = %" PRIu64 ",", station->timestamp);
  tsig_log_dbg("  .next_timestamp = %" PRIu64 ",", station->next_timestamp);
  tsig_log_dbg("  .samples_tick   = %" PRIu64 ",", station->samples_tick);
  tsig_log_dbg("  .samples        = %" PRIu64 ",", station->samples);
  tsig_log_dbg("  .next_tick      = %" PRIu64 ",", station->next_tick);
  tsig_log_dbg("  .tick           = %" PRIu16 ",", station->tick);
  tsig_log_dbg("  .is_morse       = %d,", station->is_morse);
  tsig_log_dbg("  .iir            = {");
  tsig_log_dbg("    .freq    = %" PRIu32 ",", station->iir.freq);
  tsig_log_dbg("    .rate    = %" PRIu32 ",", station->iir.rate);
  tsig_log_dbg("    .phase   = %d,", station->iir.phase);
  tsig_log_dbg("    .a       = %f,", station->iir.a);
  tsig_log_dbg("    .period  = %" PRIu32 ",", station->iir.period);
  tsig_log_dbg("    .init_y0 = %f,", station->iir.init_y0);
  tsig_log_dbg("    .init_y1 = %f,", station->iir.init_y1);
  tsig_log_dbg("    .sample  = %" PRIu32 ",", station->iir.sample);
  tsig_log_dbg("    .y0      = %f,", station->iir.y0);
  tsig_log_dbg("    .y1      = %f,", station->iir.y1);
  tsig_log_dbg("  },");
  tsig_log_dbg("  .freq           = %" PRIu32 ",", station->freq);
  tsig_log_dbg("  .gain           = %f,", station->gain);
  tsig_log_dbg("  .verbose        = %d,", station->verbose);
  tsig_log_dbg("  .log            = %p,", station->log);
  tsig_log_dbg("};");
}
#endif /* TSIG_DEBUG */

/**
 * Time station waveform generator callback function.
 *
 * This callback is invoked by an audio backend to generate some samples
 * whenever the output buffer has been sufficiently drained to accept more.
 *
 * @param cb_data Initialized station waveform generator context.
 *  This is a `tsig_station_t *` intentionally passed as a `void *`.
 * @param[out] out_cb_buf Buffer to be filled with 1ch 64-bit float samples.
 * @param size Count of samples to be generated.
 */
void tsig_station_cb(void *cb_data, double *out_cb_buf, uint32_t size) {
  tsig_station_t *station = cb_data;

  station_info_t *info = &station_info[station->station];
  bool is_jjy = station->station == TSIG_STATION_ID_JJY ||
                station->station == TSIG_STATION_ID_JJY60;
  uint64_t timestamp = tsig_datetime_get_timestamp();
  uint64_t expected = station->next_timestamp;
  char msg[TSIG_STATION_MESSAGE_SIZE];
  tsig_log_t *log = station->log;
  tsig_datetime_t datetime;
  uint64_t elapsed_msecs;
  uint64_t drift;

  /*
   * On first run, calculate the offset to apply to the system time such
   * that we start transmitting from the configured time base + user offset.
   */

  if (expected == station_first_run)
    station->base_offset =
        station->base != TSIG_STATION_BASE_SYSTEM
            ? station->base - (int64_t)timestamp + station->offset
            : station->offset;

  /*
   * This calculation may overflow if the time base is close to the start
   * of the epoch and the user offset is negative and/or the system clock
   * is set (far) backward during runtime. Resolution: worksforme, wontfix~
   */

  timestamp += station->base_offset;

  /* Resync on first run, sample rate change, or clock drift (e.g. NTP). */
  drift = timestamp > expected ? timestamp - expected : expected - timestamp;
  if (drift > station_drift_threshold) {
    datetime = tsig_datetime_parse_timestamp(timestamp);

    uint32_t msecs_since_tick = datetime.msec % TSIG_STATION_MSECS_TICK;
    uint32_t msecs_to_tick = TSIG_STATION_MSECS_TICK - msecs_since_tick;
    uint32_t msecs_since_min = 1000 * datetime.sec + datetime.msec;

    station->timestamp = timestamp;
    station->samples = 0;
    station->next_tick = msecs_to_tick * station->rate / 1000;
    station->tick = msecs_since_min / TSIG_STATION_MSECS_TICK;
    station->is_morse = is_jjy &&
                        (datetime.min == station_jjy_morse_min ||
                         datetime.min == station_jjy_morse_min2) &&
                        station_jjy_morse_tick <= station->tick &&
                        station->tick < station_jjy_morse_end_tick;

    /*
     * Per DCF77's signal format specification, each minute and each transmit
     * power change occurs at a rising zero crossing. We don't have enough
     * control over what actually gets transmitted to reliably emulate this,
     * and it's almost certainly not necessary for our purposes. Still,
     * there's no particular reason not to try, so adjust the initial phase
     * of the waveform such that the beginning of the next minute occurs at
     * such a crossing. The phase change shouldn't matter for other stations.
     */

    uint32_t msecs_to_min = station_msecs_min - msecs_since_min;
    int32_t to_min = msecs_to_min * station->rate / 1000;
    uint32_t iir_freq = station->freq;

#ifdef TSIG_DEBUG
    iir_freq = 1000;
#endif /* TSIG_DEBUG */

    tsig_iir_init(&station->iir, iir_freq, station->rate, -to_min);

    info->update_cb(station, timestamp);
    info->status_cb(station, timestamp);

    /* clang-format off */
    sprintf(msg, /* "%04hu-%02hhu-%02hhu %02hhu:%02hhu:%02hhu.%03hu" */
            "%04" PRIu16 "-%02" PRIu8 "-%02" PRIu8
            " %02" PRIu8 ":%02" PRIu8 ":%02" PRIu8 ".%03" PRIu16,
            datetime.year, datetime.mon, datetime.day,
            datetime.hour, datetime.min, datetime.sec, datetime.msec);
    /* clang-format on */

    if (expected && expected != station_first_run)
      tsig_log_note("Resynced to %s UTC (delta %s%" PRIu64 " ms).", msg,
                    timestamp < expected ? "-" : "+", drift);
    else
      tsig_log("Synced to %s UTC.", msg);

#ifdef TSIG_DEBUG
    station_print(station);
#endif /* TSIG_DEBUG */
  }

  /* Fill the output buffer. */
  uint8_t xmit_bit = xmit_bit = 1 << (station->tick % CHAR_BIT);
  uint8_t xmit_i = station->tick / CHAR_BIT;

  for (uint32_t i = 0; i < size; i++) {
    /* Update state on each tick. */
    if (station->samples == station->next_tick) {
      elapsed_msecs = station->samples * 1000 / station->rate;
      timestamp = station->timestamp + elapsed_msecs;
      datetime = tsig_datetime_parse_timestamp(timestamp);

      station->next_tick += station->samples_tick;
      station->tick = (station->tick + 1) % TSIG_STATION_TICKS_MIN;

      if (!station->tick) {
        info->update_cb(station, timestamp);

        /* clang-format off */
        tsig_log_dbg(/* "Synced at %04hu-%02hhu-%02hhu %02hhu:%02hhu UTC." */
                     /* e.g. "Synced at 2099-12-31 12:34 UTC." */
                     "Synced at %04" PRIu16 "-%02" PRIu8 "-%02" PRIu8
                     " %02" PRIu8 ":%02" PRIu8 " UTC.",
                     datetime.year, datetime.mon, datetime.day,
                     datetime.hour, datetime.min);
        /* clang-format on */

#ifdef TSIG_DEBUG
        station_print(station);
#endif /* TSIG_DEBUG */
      }

      if (!(station->tick % TSIG_STATION_TICKS_SEC))
        info->status_cb(station, timestamp);

      /*
       * Using a public WebSDR, it was determined that if JJY is doing an
       * announcement, it transmits its callsign in Morse code from about
       * 40.550 to 48.250 seconds after the minute. During this time, keying is
       * on-off and low gain is 0 instead of the usual -10 dB. Afterwards, low
       * gain delays returning to -10 dB until the marker bit at 49 seconds.
       */

      if (is_jjy && (datetime.min == station_jjy_morse_min ||
                     datetime.min == station_jjy_morse_min2)) {
        if (station->tick == station_jjy_morse_tick)
          station->is_morse = true;
        else if (station->tick == station_jjy_morse_end_tick)
          station->is_morse = false;
      }

      xmit_bit = (xmit_bit << 1) | (xmit_bit >> 7);
      xmit_i = station->tick / CHAR_BIT;
    }

    /* Find the nominal gain for this sample. */
    bool is_xmit_high = station->xmit_level[xmit_i] & xmit_bit;
    double target_gain = is_xmit_high        ? 1.0
                         : station->is_morse ? 0.0
                                             : info->xmit_low;

    /* Interpolate a rapid gain change if needed. */
    if (station->smooth)
      station->gain = station_lerp(target_gain, station->gain);
    else
      station->gain = target_gain;

    /* Generate a sample. */
    out_cb_buf[i] = tsig_iir_next(&station->iir) * station->gain;

    station->samples++;
  }

  /* Compute the next timestamp at which this callback will be invoked. */
  elapsed_msecs = station->samples * 1000 / station->rate;
  station->next_timestamp = station->timestamp + elapsed_msecs;
}

/**
 * Initialize a time station waveform generator context.
 *
 * @param station Uninitialized station waveform generator context.
 * @param cfg Initialized program configuration.
 * @param log Initialized logging context.
 */
void tsig_station_init(tsig_station_t *station, tsig_cfg_t *cfg,
                       tsig_log_t *log) {
  uint32_t freq = station_info[cfg->station].freq;
  uint32_t limit = station_ultrasound_threshold;
  tsig_station_id_t station_id = cfg->station;
  bool ultrasound = cfg->ultrasound;
  int32_t offset = cfg->offset;
  bool verbose = cfg->verbose;
  uint32_t rate = cfg->rate;
  bool smooth = cfg->smooth;
  int64_t base = cfg->base;
  int16_t dut1 = cfg->dut1;
  uint32_t subharmonic = 1;

  /*
   * The first odd-numbered subharmonic of the station frequency that falls
   * within the Nyquist frequency for a supported output sample rate is below:
   *
   *                                     Output sample rate
   *                   44100  48000  88200  96000 176400 192000 352800 384000
   *
   *            40000  13333  13333  40000  40000  40000  40000  40000  40000
   * Station    60000  20000  20000  20000  20000  60000  60000  60000  60000
   * frequency  68500  13700  22833  22833  22833  68500  68500  68500  68500
   *            77500  15500  15500  25833  25833  77500  77500  77500  77500
   *
   * Audio equipment often filters out ultrasound frequencies much above 20 kHz,
   * but some equipment will play them back just fine. Attempting ultrasound
   * playback might conceivably damage certain sound cards or speakers (although
   * it would be rather unlikely), so we will do so only if the user allows it.
   */

  if (cfg->ultrasound)
    limit = cfg->rate / 2;

  while (freq / subharmonic > limit)
    subharmonic += 2;

  *station = (tsig_station_t){
      .station = station_id,
      .base = base,
      .offset = offset,
      .dut1 = dut1,
      .smooth = smooth,
      .rate = rate,
      .xmit_level = {0},
      .xmit = {""},
      .meaning = {""},
      .next_timestamp = station_first_run,
      .samples_tick = rate * TSIG_STATION_MSECS_TICK / 1000,
      .freq = freq / subharmonic,
      .verbose = verbose,
      .log = log,
  };

  station_init_print(log, station_id, base, offset, dut1, smooth, ultrasound,
                     freq, subharmonic);
}

/**
 * Set the sample rate for a time station waveform generator context.
 *
 * @param station Initialized station waveform generator context.
 * @param rate Sample rate.
 */
void tsig_station_set_rate(tsig_station_t *station, uint32_t rate) {
  station->rate = rate;
  station->samples_tick = rate * TSIG_STATION_MSECS_TICK / 1000;
  station->next_timestamp = 0; /* Force a resync when possible. */
}

/**
 * Match a time station name to its station ID.
 *
 * @param name Time station name.
 * @return Time station ID, or TSIG_STATION_ID_UNKNOWN if invalid.
 */
tsig_station_id_t tsig_station_id(const char *name) {
  tsig_station_id_t value = tsig_mapping_match_key(station_ids, name);
  return value < 0 ? TSIG_STATION_ID_UNKNOWN : value;
}

/**
 * Match a time station ID to its name.
 *
 * @param station_id Time station ID.
 * @return Time station name, or NULL if invalid.
 */
const char *tsig_station_name(tsig_station_id_t station_id) {
  return tsig_mapping_match_value(station_ids, station_id);
}
