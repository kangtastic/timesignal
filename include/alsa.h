/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * alsa.h: Header for ALSA output facilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#pragma once

#include "audio.h"

#include <alsa/asoundlib.h>

typedef struct tsig_cfg tsig_cfg_t;
typedef struct tsig_log tsig_log_t;

/** ALSA output context. */
typedef struct tsig_alsa {
  snd_pcm_t *pcm; /** PCM handle. */
  char *device;   /** Device name. */

  snd_pcm_access_t access;       /** Access type. */
  snd_pcm_format_t format;       /** Sample format. */
  unsigned rate;                 /** Sample rate. */
  unsigned channels;             /** Channel count. */
  snd_pcm_uframes_t buffer_size; /** Buffer size. */
  snd_pcm_uframes_t period_size; /** Period size. */

  snd_pcm_uframes_t start_threshold; /** Start threshold. */
  snd_pcm_uframes_t avail_min;       /** Fill threshold. */

  tsig_audio_format_t audio_format; /** Sample format ID. */
  tsig_log_t *log;                  /** Logging context. */
} tsig_alsa_t;

int tsig_alsa_init(tsig_alsa_t *alsa, tsig_cfg_t *cfg, tsig_log_t *log);
int tsig_alsa_loop(tsig_alsa_t *alsa, tsig_audio_cb_t cb, void *cb_data);
int tsig_alsa_deinit(tsig_alsa_t *alsa);
