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

/** Buffer size. */
#define TSIG_STATION_MESSAGE_SIZE 128

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

/** Pointer to a function that generates transmit level flags. */
typedef void (*station_xmit_cb_t)(tsig_station_t *station,
                                  int64_t utc_timestamp);

/** Functions that generate transmit level flags. */
static void station_xmit_bpc(tsig_station_t *station, int64_t utc_timestamp);
static void station_xmit_dcf77(tsig_station_t *station, int64_t utc_timestamp);
static void station_xmit_jjy(tsig_station_t *station, int64_t utc_timestamp);
static void station_xmit_msf(tsig_station_t *station, int64_t utc_timestamp);
static void station_xmit_wwvb(tsig_station_t *station, int64_t utc_timestamp);

/** Characteristics of a real time station's signal. */
typedef struct station_info {
  station_xmit_cb_t xmit_cb; /** Transmit level flags generator. */
  int32_t utc_offset;        /** Usual (not summer time) UTC offset. */
  uint32_t freq;             /** Actual broadcast frequency. */
  double xmit_low;           /** Low gain in [0.0-1.0]. */
} station_info_t;

static station_info_t station_info[] = {
    [TSIG_STATION_ID_BPC] =
        {
            .xmit_cb = station_xmit_bpc,
            .utc_offset = 28800000, /* CST is UTC+0800 */
            .freq = 68500,
            .xmit_low = 3.162277660168379411765e-01, /* -10 dB */
        },
    [TSIG_STATION_ID_DCF77] =
        {
            .xmit_cb = station_xmit_dcf77,
            .utc_offset = 3600000, /* CET is UTC+0100 */
            .freq = 77500,
            .xmit_low = 1.496235656094433430496e-01, /* -16.5 dB */
        },
    [TSIG_STATION_ID_JJY] =
        {
            .xmit_cb = station_xmit_jjy,
            .utc_offset = 32400000, /* JST is UTC+0900 */
            .freq = 40000,
            .xmit_low = 3.162277660168379411765e-01, /* -10 dB */
        },
    [TSIG_STATION_ID_JJY60] =
        {
            .xmit_cb = station_xmit_jjy,
            .utc_offset = 32400000, /* JST is UTC+0900 */
            .freq = 60000,
            .xmit_low = 3.162277660168379411765e-01, /* -10 dB */
        },
    [TSIG_STATION_ID_MSF] =
        {
            .xmit_cb = station_xmit_msf,
            .utc_offset = 0, /* UTC */
            .freq = 60000,
            .xmit_low = 0.0, /* On-off keying */
        },
    [TSIG_STATION_ID_WWVB] =
        {
            .xmit_cb = station_xmit_wwvb,
            .utc_offset = 0, /* UTC */
            .freq = 60000,
            .xmit_low = 1.412537544622754492885e-01, /* -17 dB */
        },
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

/** Compute transmit level flags for BPC. */
static void station_xmit_bpc(tsig_station_t *station, int64_t utc_timestamp) {
  tsig_datetime_t datetime = tsig_datetime_parse_timestamp(
      utc_timestamp + station_info[TSIG_STATION_ID_BPC].utc_offset);
  uint8_t bits[20] = {[0] = station_sync_marker};

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

/** Compute transmit level flags for DCF77. */
static void station_xmit_dcf77(tsig_station_t *station, int64_t utc_timestamp) {
  tsig_datetime_t utc_datetime = tsig_datetime_parse_timestamp(utc_timestamp);

  tsig_datetime_t datetime = tsig_datetime_parse_timestamp(
      utc_timestamp + station_info[TSIG_STATION_ID_DCF77].utc_offset);
  uint8_t bits[60] = {[20] = 1, [59] = station_sync_marker};

  /* Transmitted time is the CET/CEST time at the next UTC minute. */
  int32_t in_mins;
  bool is_cest = tsig_datetime_is_eu_dst(utc_datetime, &in_mins);
  bool is_xmit_cest = is_cest ^ (in_mins == 1);

  bits[16] = 1 <= in_mins && in_mins <= 60;
  bits[17] = is_xmit_cest;
  bits[18] = !is_xmit_cest;

  uint32_t cest_offset = is_xmit_cest ? station_msecs_hour : 0;
  uint32_t xmit_offset = station_msecs_min;
  int64_t xmit_timestamp = datetime.timestamp + cest_offset + xmit_offset;
  tsig_datetime_t xmit_datetime = tsig_datetime_parse_timestamp(xmit_timestamp);

  bits[20] = 1;

  uint8_t min = xmit_datetime.min % 10;
  bits[21] = min & 1;
  bits[22] = min & 2;
  bits[23] = min & 4;
  bits[24] = min & 8;

  uint8_t min_10 = xmit_datetime.min / 10;
  bits[25] = min_10 & 1;
  bits[26] = min_10 & 2;
  bits[27] = min_10 & 4;

  bits[28] = station_even_parity(bits, 21, 28);

  uint8_t hour = xmit_datetime.hour % 10;
  bits[29] = hour & 1;
  bits[30] = hour & 2;
  bits[31] = hour & 4;
  bits[32] = hour & 8;

  uint8_t hour_10 = xmit_datetime.hour / 10;
  bits[33] = hour_10 & 1;
  bits[34] = hour_10 & 2;

  bits[35] = station_even_parity(bits, 29, 35);

  uint8_t day = xmit_datetime.day % 10;
  bits[36] = day & 1;
  bits[37] = day & 2;
  bits[38] = day & 4;
  bits[39] = day & 8;

  uint8_t day_10 = xmit_datetime.day / 10;
  bits[40] = day_10 & 1;
  bits[41] = day_10 & 2;

  uint8_t dow = xmit_datetime.dow ? xmit_datetime.dow : 7;
  bits[42] = dow & 1;
  bits[43] = dow & 2;
  bits[44] = dow & 4;

  uint8_t mon = xmit_datetime.mon % 10;
  bits[45] = mon & 1;
  bits[46] = mon & 2;
  bits[47] = mon & 4;
  bits[48] = mon & 8;

  uint8_t mon_10 = xmit_datetime.mon / 10;
  bits[49] = mon_10 & 1;

  uint8_t year = xmit_datetime.year % 10;
  bits[50] = year & 1;
  bits[51] = year & 2;
  bits[52] = year & 4;
  bits[53] = year & 8;

  uint8_t year_10 = (xmit_datetime.year % 100) / 10;
  bits[54] = year_10 & 1;
  bits[55] = year_10 & 2;
  bits[56] = year_10 & 4;
  bits[57] = year_10 & 8;

  bits[58] = station_even_parity(bits, 36, 58);

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

/** Compute transmit level flags for JJY and JJY60. */
static void station_xmit_jjy(tsig_station_t *station, int64_t utc_timestamp) {
  tsig_datetime_t datetime = tsig_datetime_parse_timestamp(
      utc_timestamp + station_info[TSIG_STATION_ID_JJY].utc_offset);
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

  bits[36] = station_even_parity(bits, 12, 19);
  bits[37] = station_even_parity(bits, 1, 9);

  bool is_announce = datetime.min == station_jjy_morse_min ||
                     datetime.min == station_jjy_morse_min2;
  if (!is_announce) {
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

/** Compute transmit level flags for MSF. */
static void station_xmit_msf(tsig_station_t *station, int64_t utc_timestamp) {
  tsig_datetime_t utc_datetime = tsig_datetime_parse_timestamp(utc_timestamp);

  tsig_datetime_t datetime = tsig_datetime_parse_timestamp(
      utc_timestamp + station_info[TSIG_STATION_ID_MSF].utc_offset);
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

  /* Transmitted time is the UTC/BST time at the next UTC minute. */
  bool is_xmit_bst = is_bst ^ (in_mins == 1);
  uint32_t bst_offset = is_xmit_bst ? station_msecs_hour : 0;
  uint32_t xmit_offset = station_msecs_min;
  int64_t xmit_timestamp = datetime.timestamp + bst_offset + xmit_offset;
  tsig_datetime_t xmit_datetime = tsig_datetime_parse_timestamp(xmit_timestamp);

  uint8_t year_10 = (xmit_datetime.year % 100) / 10;
  bits[17] = year_10 & 8;
  bits[18] = year_10 & 4;
  bits[19] = year_10 & 2;
  bits[20] = year_10 & 1;

  uint8_t year = xmit_datetime.year % 10;
  bits[21] = year & 8;
  bits[22] = year & 4;
  bits[23] = year & 2;
  bits[24] = year & 1;

  uint8_t mon_10 = xmit_datetime.mon / 10;
  bits[25] = mon_10 & 1;

  uint8_t mon = xmit_datetime.mon % 10;
  bits[26] = mon & 8;
  bits[27] = mon & 4;
  bits[28] = mon & 2;
  bits[29] = mon & 1;

  uint8_t day_10 = xmit_datetime.day / 10;
  bits[30] = day_10 & 2;
  bits[31] = day_10 & 1;

  uint8_t day = xmit_datetime.day % 10;
  bits[32] = day & 8;
  bits[33] = day & 4;
  bits[34] = day & 2;
  bits[35] = day & 1;

  uint8_t dow = xmit_datetime.dow;
  bits[36] = dow & 4;
  bits[37] = dow & 2;
  bits[38] = dow & 1;

  uint8_t hour_10 = xmit_datetime.hour / 10;
  bits[39] = hour_10 & 2;
  bits[40] = hour_10 & 1;

  uint8_t hour = xmit_datetime.hour % 10;
  bits[41] = hour & 8;
  bits[42] = hour & 4;
  bits[43] = hour & 2;
  bits[44] = hour & 1;

  uint8_t min_10 = xmit_datetime.min / 10;
  bits[45] = min_10 & 4;
  bits[46] = min_10 & 2;
  bits[47] = min_10 & 1;

  uint8_t min = xmit_datetime.min % 10;
  bits[48] = min & 8;
  bits[49] = min & 4;
  bits[50] = min & 2;
  bits[51] = min & 1;

  bits[53] = 1 <= in_mins && in_mins <= 61;
  bits[54] = station_odd_parity(bits, 17, 25);
  bits[55] = station_odd_parity(bits, 25, 36);
  bits[56] = station_odd_parity(bits, 36, 39);
  bits[57] = station_odd_parity(bits, 39, 52);
  bits[58] = is_xmit_bst;

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

/** Compute transmit level flags for WWVB. */
static void station_xmit_wwvb(tsig_station_t *station, int64_t utc_timestamp) {
  tsig_datetime_t utc_datetime = tsig_datetime_parse_timestamp(utc_timestamp);

  tsig_datetime_t datetime = tsig_datetime_parse_timestamp(
      utc_timestamp + station_info[TSIG_STATION_ID_WWVB].utc_offset);
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

  bits[55] = tsig_datetime_is_leap(utc_datetime.year);

  bool is_dst_end;
  bits[58] = tsig_datetime_is_us_dst(utc_datetime, &is_dst_end);
  bits[57] = is_dst_end;

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

/** Print transmit level flags. */
void station_xmit_level_print(tsig_log_t *log, uint8_t xmit_level[]) {
#ifndef TSIG_DEBUG
  tsig_log_dbg("transmit level flags for this minute:");
#endif /* TSIG_DEBUG */
  uint8_t bit = 0x01;
  uint8_t i = 0;
  char msg[TSIG_STATION_MESSAGE_SIZE];
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

#ifdef TSIG_DEBUG
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
  tsig_log_t *log = station->log;

  station_info_t *info = &station_info[station->station];
  bool is_jjy = station->station == TSIG_STATION_ID_JJY ||
                station->station == TSIG_STATION_ID_JJY60;

  uint64_t now = tsig_datetime_get_timestamp();
  uint64_t expected = station->next_timestamp;

  /*
   * On first run, calculate the offset to apply to the system time such
   * that we start transmitting from the configured time base + user offset.
   */

  if (expected == station_first_run)
    station->base_offset = station->base != TSIG_STATION_BASE_SYSTEM
                               ? station->base - (int64_t)now + station->offset
                               : station->offset;

  /*
   * This calculation may overflow if the time base is close to the start
   * of the epoch and the user offset is negative and/or the system clock
   * is set (far) backward during runtime. Resolution: worksforme, wontfix~
   */

  now += station->base_offset;

  /* Resync on first run, sample rate change, or clock drift (e.g. NTP). */
  uint64_t drift = now > expected ? now - expected : expected - now;

  if (drift > station_drift_threshold) {
    tsig_datetime_t datetime = tsig_datetime_parse_timestamp(now);

    uint32_t msecs_since_tick = datetime.msec % TSIG_STATION_MSECS_TICK;
    uint32_t msecs_to_tick = TSIG_STATION_MSECS_TICK - msecs_since_tick;
    uint32_t msecs_since_min = 1000 * datetime.sec + datetime.msec;
    uint32_t msecs_to_min = station_msecs_min - msecs_since_min;
    int32_t to_min = msecs_to_min * station->rate / 1000;

    info->xmit_cb(station, now);

    station->timestamp = now;
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

#ifndef TSIG_DEBUG
    tsig_iir_init(&station->iir, station->freq, station->rate, -to_min);
#else
    tsig_iir_init(&station->iir, 1000, station->rate, -to_min);
#endif /* TSIG_DEBUG */

    char msg[TSIG_STATION_MESSAGE_SIZE];

    /* clang-format off */
    sprintf(msg, /* "%04hu-%02hhu-%02hhu %02hhu:%02hhu:%02hhu.%03hu" */
            "%04" PRIu16 "-%02" PRIu8 "-%02" PRIu8
            " %02" PRIu8 ":%02" PRIu8 ":%02" PRIu8 ".%03" PRIu16,
            datetime.year, datetime.mon, datetime.day,
            datetime.hour, datetime.min, datetime.sec, datetime.msec);
    /* clang-format on */

    if (expected && expected != station_first_run) {
      tsig_log_note("Resynced to %s UTC.", msg);
      tsig_log_note("System clock was %" PRIu64 " ms %s than expected.", drift,
                    now < expected ? "less" : "more");
    } else {
      tsig_log("Synced to %s UTC.", msg);
    }

#ifndef TSIG_DEBUG
    station_xmit_level_print(log, station->xmit_level);
#else
    station_print(station);
#endif /* TSIG_DEBUG */
  }

  /* Fill the output buffer. */
  uint8_t xmit_bit = xmit_bit = 1 << (station->tick % CHAR_BIT);
  uint8_t xmit_i = station->tick / CHAR_BIT;

  for (uint32_t i = 0; i < size; i++) {
    /* Update state on each tick. */
    if (station->samples == station->next_tick) {
      uint64_t tick_timestamp =
          station->timestamp + station->samples * 1000 / station->rate;
      tsig_datetime_t tick_datetime =
          tsig_datetime_parse_timestamp(tick_timestamp);

      station->next_tick += station->samples_tick;

      if (station->tick == TSIG_STATION_TICKS_MIN - 1) {
        /* clang-format off */
        tsig_log(/* "Synced at %04hu-%02hhu-%02hhu %02hhu:%02hhu UTC." */
                 "Synced at %04" PRIu16 "-%02" PRIu8 "-%02" PRIu8
                 " %02" PRIu8 ":%02" PRIu8 " UTC.",
                 tick_datetime.year, tick_datetime.mon, tick_datetime.day,
                 tick_datetime.hour, tick_datetime.min);
        /* clang-format on */

        info->xmit_cb(station, tick_timestamp);
        station->tick = 0;

#ifndef TSIG_DEBUG
        station_xmit_level_print(log, station->xmit_level);
#else
        station_print(station);
#endif /* TSIG_DEBUG */
      } else {
        station->tick++;
      }

      /*
       * Using a public WebSDR, it was determined that if JJY is doing an
       * announcement, it transmits its callsign in Morse code from about
       * 40.550 to 48.250 seconds after the minute. During this time, keying is
       * on-off and low gain is 0 instead of the usual -10 dB. Afterwards, low
       * gain delays returning to -10 dB until the marker bit at 49 seconds.
       */

      if (is_jjy && (tick_datetime.min == station_jjy_morse_min ||
                     tick_datetime.min == station_jjy_morse_min2)) {
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
  uint64_t elapsed_msecs = station->samples * 1000 / station->rate;
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
  tsig_station_id_t station_id = cfg->station;
  bool ultrasound = cfg->ultrasound;
  int32_t offset = cfg->offset;
  uint32_t rate = cfg->rate;
  bool smooth = cfg->smooth;
  int64_t base = cfg->base;
  int16_t dut1 = cfg->dut1;

  *station = (tsig_station_t){
      .station = station_id,
      .base = base,
      .offset = offset,
      .dut1 = dut1,
      .smooth = smooth,
      .rate = rate,
      .xmit_level = {0},
      .next_timestamp = station_first_run,
      .samples_tick = rate * TSIG_STATION_MSECS_TICK / 1000,
      .log = log,
  };

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

  uint32_t limit = ultrasound ? rate / 2 : station_ultrasound_threshold;
  uint32_t freq = station_info[station_id].freq;
  uint32_t subharmonic = 1;

  while (freq / subharmonic > limit)
    subharmonic += 2;

  station->freq = freq / subharmonic;

  char msg[TSIG_STATION_MESSAGE_SIZE];
  const char *sign = offset < 0 ? "-" : "";
  int32_t coeff = offset < 0 ? -1 : 1;
  tsig_datetime_t datetime = tsig_datetime_parse_timestamp(coeff * offset);
  int len;

  /* clang-format off */
  len = sprintf(msg, "Starting %s", tsig_station_name(station_id));
  if (base >= 0) {
    tsig_datetime_t base_datetime = tsig_datetime_parse_timestamp(base);
    len += sprintf(&msg[len],
                   /* " from %04hu-%02hhu-%02hhu %02hhu:%02hhu:%02hhu" */
                   " from %04" PRIu16 "-%02" PRIu8 "-%02" PRIu8
                   " %02" PRIu8 ":%02" PRIu8 ":%02" PRIu8,
                   base_datetime.year, base_datetime.mon, base_datetime.day,
                   base_datetime.hour, base_datetime.min, base_datetime.sec);
  }
  len += sprintf(
      &msg[len], /* " adjusted by %s%02hhu:%02hhu:%02hhu.%03hu" */
      " adjusted by %s%02" PRIu8 ":%02" PRIu8 ":%02" PRIu8 ".%03" PRIu16,
      sign, datetime.hour, datetime.min, datetime.sec, datetime.msec);
  if (station_id == TSIG_STATION_ID_MSF || station_id == TSIG_STATION_ID_WWVB)
    len += sprintf(&msg[len], ", DUT1 %" PRIi16 " ms", dut1);

  tsig_log("%s.", msg);

  tsig_log_dbg("gain smoothing %s, ultrasound output %s",
               smooth ? "on" : "off", ultrasound ? "allowed" : "not allowed");
  tsig_log_dbg("generating %" PRIu32 " Hz carrier"
               " (subharmonic %" PRIu32 " of %" PRIu32 " Hz)",
               freq / subharmonic, subharmonic, freq);
  /* clang-format on */
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
