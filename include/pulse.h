/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * pulse.h: Header for PulseAudio output facilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#pragma once

#include "audio.h"

#include <stdint.h>

#include <pulse/pulseaudio.h>

typedef struct tsig_cfg tsig_cfg_t;
typedef struct tsig_log tsig_log_t;

/** PulseAudio output context. */
typedef struct tsig_pulse {
  pa_mainloop *loop;        /** Loop. */
  pa_context *ctx;          /** Context. */
  pa_context_state_t state; /** Context state. */

  pa_sample_format_t format; /** Sample format. */
  uint32_t rate;             /** Sample rate. */
  uint8_t channels;          /** Channel count. */

  tsig_audio_cb_t cb; /** Sample generator callback. */
  void *cb_data;      /** Sample generator callback context object. */
  double *cb_buf;     /** Sample generator callback output buffer. */
  uint8_t *buf;       /** Client-side PulseAudio output buffer. */
  uint32_t stride;    /** Stride (i.e. audio frame size). */
  uint32_t size;      /** PulseAudio output buffer size. */

  tsig_audio_format_t audio_format; /** Sample format ID. */
  unsigned timeout;                 /** User timeout in seconds. */
  tsig_log_t *log;                  /** Logging context. */
} tsig_pulse_t;

int tsig_pulse_init(tsig_pulse_t *pulse, tsig_cfg_t *cfg, tsig_log_t *log);
int tsig_pulse_loop(tsig_pulse_t *pulse, tsig_audio_cb_t cb, void *cb_data);
int tsig_pulse_deinit(tsig_pulse_t *pulse);
