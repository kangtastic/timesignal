/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * station.h: Header for time station waveform generator.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#pragma once

#include "iir.h"
#include "log.h"

#include <alsa/asoundlib.h>

#include <stdint.h>
#include <limits.h>

/** Our internal time quantum is a "tick". */
#define TSIG_STATION_MSECS_TICK 50
#define TSIG_STATION_TICKS_SEC  (1000 / TSIG_STATION_MSECS_TICK)
#define TSIG_STATION_TICKS_MIN  (60 * TSIG_STATION_TICKS_SEC)

/** Time station IDs. */
typedef enum tsig_station_id {
  TSIG_STATION_ID_UNKNOWN = -1,
  TSIG_STATION_ID_BPC,
  TSIG_STATION_ID_DCF77,
  TSIG_STATION_ID_JJY,
  TSIG_STATION_ID_JJY60,
  TSIG_STATION_ID_MSF,
  TSIG_STATION_ID_WWVB,
} tsig_station_id_t;

/** Time station waveform generator context. */
typedef struct tsig_station {
  tsig_station_id_t station; /** Time station ID. */
  int32_t offset;            /** User offset in milliseconds. */
  int16_t dut1;              /** DUT1 value in milliseconds. */
  bool smooth;               /** Whether to interpolate rapid gain changes. */
  uint32_t sample_rate;      /** Sample rate. */

  /** Bitfield of per-tick transmit level flags for current station minute. */
  uint8_t xmit_level[TSIG_STATION_TICKS_MIN / CHAR_BIT];

  uint64_t timestamp;      /** Base timestamp of this station context. */
  uint64_t next_timestamp; /** Expected timestamp when next invoked. */
  uint64_t samples_tick;   /** Sample count per tick. */
  uint64_t samples;        /** Sample count since base timestamp. */
  uint64_t next_tick;      /** Sample count at next tick. */
  uint16_t tick;           /** Tick index within current station minute. */
  bool is_morse;           /** Whether JJY/JJY60 is announcing its callsign. */

  tsig_iir_t iir; /** IIR filter sine wave generator. */
  uint32_t freq;  /** Target waveform frequency. */
  double gain;    /** Actual current gain in [0.0-1.0]. */

  tsig_log_t *log; /** Logging context. */
} tsig_station_t;

void tsig_station_cb(void *, double *, snd_pcm_uframes_t);
void tsig_station_init(tsig_station_t *, tsig_log_t *, tsig_station_id_t,
                       int32_t, int16_t, bool, bool, uint32_t);
tsig_station_id_t tsig_station_id(const char *);
const char *tsig_station_name(tsig_station_id_t);
