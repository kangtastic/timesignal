/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * alsa.h: Header for sound output facilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#pragma once

#include "cfg.h"
#include "log.h"

#include <alsa/asoundlib.h>

/**
 * Pointer to sample generator callback function.
 *
 * @param cb_data Callback function context object.
 * @param[out] out_cb_buf Buffer to be filled with 1ch 64-bit float samples.
 * @param size Count of samples to be generated.
 */
typedef void (*tsig_alsa_cb_t)(void *cb_data, double out_cb_buf[],
                               snd_pcm_uframes_t size);

/** Sound output context. */
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

  tsig_log_t *log; /** Logging context. */
} tsig_alsa_t;

int tsig_alsa_init(tsig_alsa_t *, tsig_cfg_t *, tsig_log_t *);
int tsig_alsa_loop(tsig_alsa_t *, tsig_alsa_cb_t, void *);
int tsig_alsa_deinit(tsig_alsa_t *);
