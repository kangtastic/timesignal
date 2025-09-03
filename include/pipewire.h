/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * pipewire.h: Header for PipeWire output facilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#pragma once

#include "audio.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/raw.h>

#include <stdint.h>

typedef struct tsig_cfg tsig_cfg_t;
typedef struct tsig_log tsig_log_t;

/** PipeWire output context. */
typedef struct tsig_pipewire {
  struct pw_main_loop *loop; /** Loop. */
  struct pw_stream *stream;  /** Stream. */
  int loop_ret;              /** Loop return value. */

  enum spa_audio_format format; /** Sample format. */
  uint32_t rate;                /** Sample rate. */
  uint16_t channels;            /** Channel count. */

  tsig_audio_cb_t cb; /** Sample generator callback. */
  void *cb_data;      /** Sample generator callback context object. */
  double *cb_buf;     /** Sample generator callback output buffer. */
  uint32_t stride;    /** Stride (i.e. audio frame size). */
  uint32_t size;      /** PipeWire output buffer size. */

  tsig_audio_format_t audio_format; /** Sample format ID. */
  unsigned timeout;                 /** User timeout in seconds. */
  tsig_log_t *log;                  /** Logging context. */
} tsig_pipewire_t;

int tsig_pipewire_lib_init(tsig_log_t *log);
int tsig_pipewire_init(tsig_pipewire_t *pipewire, tsig_cfg_t *cfg,
                       tsig_log_t *log);
int tsig_pipewire_loop(tsig_pipewire_t *pipewire, tsig_audio_cb_t cb,
                       void *cb_data);
int tsig_pipewire_deinit(tsig_pipewire_t *pipewire);
int tsig_pipewire_lib_deinit(tsig_log_t *log);
