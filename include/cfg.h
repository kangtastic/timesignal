/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * cfg.h: Header for program configuration.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#pragma once

#include "log.h"

#include <alsa/asoundlib.h>

#include <stdint.h>
#include <stdbool.h>

/** Program configuration initialization results. */
typedef enum tsig_cfg_init_result {
  TSIG_CFG_INIT_FAIL = -1, /** Error parsing arguments. */
  TSIG_CFG_INIT_OK,        /** Completed parsing arguments. */
  TSIG_CFG_INIT_HELP,      /** User printed help, exit gracefully. */
} tsig_cfg_init_result_t;

/** Sample rates. */
typedef enum tsig_cfg_rate {
  TSIG_CFG_RATE_44100 = 44100,
  TSIG_CFG_RATE_48000 = 48000,
  TSIG_CFG_RATE_88200 = 88200,
  TSIG_CFG_RATE_96000 = 96000,
  TSIG_CFG_RATE_176400 = 176400,
  TSIG_CFG_RATE_192000 = 192000,
  TSIG_CFG_RATE_352800 = 352800,
  TSIG_CFG_RATE_384000 = 384000,
} tsig_cfg_rate_t;

/** Time stations. */
typedef enum tsig_cfg_station {
  TSIG_CFG_STATION_BPC,
  TSIG_CFG_STATION_DCF77,
  TSIG_CFG_STATION_JJY,
  TSIG_CFG_STATION_JJY60,
  TSIG_CFG_STATION_MSF,
  TSIG_CFG_STATION_WWVB,
} tsig_cfg_station_t;

/** Program configuration. */
typedef struct tsig_cfg {
  int32_t offset;             /** User offset in milliseconds. */
  tsig_cfg_station_t station; /** Time station. */
  int16_t dut1;               /** DUT1 value in milliseconds. */

  char *device;            /** Output device. */
  snd_pcm_format_t format; /** Sample format. */
  uint32_t rate;           /** Sample rate. */
  uint16_t channels;       /** Channel count. */
  bool smooth;             /** Whether to interpolate rapid gain changes. */
  bool ultrasound;         /** Whether to allow ultrasound output. */

  char *log_file; /** Path to log file. */
  bool syslog;    /** Whether to log to syslog. */

  int verbosity; /** Verbosity level. */
} tsig_cfg_t;

tsig_cfg_init_result_t tsig_cfg_init(tsig_cfg_t *, tsig_log_t *, int, char *[]);
const char *tsig_cfg_station_name(tsig_cfg_station_t);
void tsig_cfg_help(void);
