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
#include "backend.h"
#include "log.h"
#include "station.h"

#include <stdbool.h>
#include <stdint.h>

/** Buffer sizes. */
#define TSIG_CFG_PATH_SIZE 4096

#ifdef TSIG_HAVE_ALSA
#define TSIG_CFG_DEVICE_SIZE 128
#endif /* TSIG_HAVE_ALSA */

typedef struct tsig_log tsig_log_t;

/** Program configuration initialization results. */
typedef enum tsig_cfg_init_result {
  TSIG_CFG_INIT_FAIL = -1, /** Error parsing arguments. */
  TSIG_CFG_INIT_OK,        /** Completed parsing arguments. */
  TSIG_CFG_INIT_HELP,      /** User printed help, exit gracefully. */
} tsig_cfg_init_result_t;

/** Program configuration. */
typedef struct tsig_cfg {
  tsig_station_id_t station; /** Time station. */
  int64_t base;              /** Time base in milliseconds since epoch. */
  int32_t offset;            /** User offset in milliseconds. */
  int16_t dut1;              /** DUT1 value in milliseconds. */

  unsigned timeout; /** User timeout in seconds. */

  /* clang-format off */
#ifdef TSIG_HAVE_BACKENDS
  tsig_backend_t backend;     /** Audio backend. */
#endif /* TSIG_HAVE_BACKENDS */

#ifdef TSIG_HAVE_ALSA
  char device[TSIG_CFG_DEVICE_SIZE]; /** ALSA device. */
#endif /* TSIG_HAVE_ALSA */

  tsig_audio_format_t format; /** Sample format. */
  uint32_t rate;              /** Sample rate. */
  uint16_t channels;          /** Channel count. */
  bool smooth;                /** Whether to interpolate rapid gain changes. */
  bool ultrasound;            /** Whether to allow ultrasound output. */
  bool audible;               /** Whether to make output waveform audible. */
  /* clang-format on */

  char log_file[TSIG_CFG_PATH_SIZE]; /** Path to log file. */
  bool syslog;                       /** Whether to log to syslog. */
  bool verbose;                      /** Whether to be verbose. */
  bool quiet;                        /** Whether to log nothing to console. */
} tsig_cfg_t;

tsig_cfg_init_result_t tsig_cfg_init(tsig_cfg_t *cfg, tsig_log_t *log, int argc,
                                     char *argv[]);
