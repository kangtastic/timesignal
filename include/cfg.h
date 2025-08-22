/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * cfg.h: Header for program configuration.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#pragma once

#include "audio.h"
#include "log.h"
#include "station.h"

#include <stdint.h>
#include <stdbool.h>

/** Program configuration initialization results. */
typedef enum tsig_cfg_init_result {
  TSIG_CFG_INIT_FAIL = -1, /** Error parsing arguments. */
  TSIG_CFG_INIT_OK,        /** Completed parsing arguments. */
  TSIG_CFG_INIT_HELP,      /** User printed help, exit gracefully. */
} tsig_cfg_init_result_t;

/** Program configuration. */
typedef struct tsig_cfg {
  int32_t offset;            /** User offset in milliseconds. */
  tsig_station_id_t station; /** Time station. */
  int16_t dut1;              /** DUT1 value in milliseconds. */

  char *device;               /** Output device. */
  tsig_audio_format_t format; /** Sample format. */
  uint32_t rate;              /** Sample rate. */
  uint16_t channels;          /** Channel count. */
  bool smooth;                /** Whether to interpolate rapid gain changes. */
  bool ultrasound;            /** Whether to allow ultrasound output. */

  char *log_file; /** Path to log file. */
  bool syslog;    /** Whether to log to syslog. */

  int verbosity; /** Verbosity level. */
} tsig_cfg_t;

tsig_cfg_init_result_t tsig_cfg_init(tsig_cfg_t *, tsig_log_t *, int, char *[]);
void tsig_cfg_help(void);
