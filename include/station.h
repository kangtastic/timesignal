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

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct tsig_cfg tsig_cfg_t;
typedef struct tsig_log tsig_log_t;

/** Our internal time quantum is a "tick". */
#define TSIG_STATION_MSECS_TICK 50
#define TSIG_STATION_TICKS_SEC  (1000 / TSIG_STATION_MSECS_TICK)
#define TSIG_STATION_TICKS_MIN  (60 * TSIG_STATION_TICKS_SEC)

/** Our default time base is the system time. */
#define TSIG_STATION_BASE_SYSTEM -1

/** Buffer size. */
#define TSIG_STATION_MESSAGE_SIZE 128

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
  int64_t base;              /** Time base in milliseconds since epoch. */
  int32_t offset;            /** User offset in milliseconds. */
  int16_t dut1;              /** DUT1 value in milliseconds. */
  bool smooth;               /** Whether to interpolate rapid gain changes. */
  bool audible;              /** Whether to make waveform audible. */
  uint32_t rate;             /** Sample rate. */

  /** Bitfield of per-tick transmit level flags for current station minute. */
  uint8_t xmit_level[TSIG_STATION_TICKS_MIN / CHAR_BIT];

  /** Bit readout for current station minute (20 seconds for BPC). */
  char xmit[TSIG_STATION_MESSAGE_SIZE];

  /** Meaning of waveform for current station minute (20 seconds for BPC). */
  char meaning[TSIG_STATION_MESSAGE_SIZE];

  int64_t base_offset;     /** Base timestamp offset relative to system time. */
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

  bool verbose;    /** Whether to provide verbose status updates. */
  tsig_log_t *log; /** Logging context. */
} tsig_station_t;

void tsig_station_cb(void *cb_data, double *out_cb_buf, uint32_t size);
void tsig_station_init(tsig_station_t *station, tsig_cfg_t *cfg,
                       tsig_log_t *log);
void tsig_station_set_rate(tsig_station_t *station, uint32_t rate);
tsig_station_id_t tsig_station_id(const char *name);
const char *tsig_station_name(tsig_station_id_t station_id);
