/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * backend.h: Header for audio backend facilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#pragma once

#include "audio.h"

typedef struct tsig_cfg tsig_cfg_t;
typedef struct tsig_log tsig_log_t;

/** Recognized audio backends. */
typedef enum tsig_backend {
  TSIG_BACKEND_UNKNOWN = -1,

#ifdef TSIG_HAVE_PIPEWIRE
  TSIG_BACKEND_PIPEWIRE,
#endif /* TSIG_HAVE_PIPEWIRE */

#ifdef TSIG_HAVE_PULSE
  TSIG_BACKEND_PULSE,
#endif /* TSIG_HAVE_PULSE */

#ifdef TSIG_HAVE_ALSA
  TSIG_BACKEND_ALSA,
#endif /* TSIG_HAVE_ALSA */
} tsig_backend_t;

/**
 * Pointer to audio backend initialization function.
 *
 * @param ab_data Uninitialized audio backend context object.
 * @param cfg Initialized program configuration.
 * @param log Initialized logging context.
 * @return 0 upon success, negative error code upon error.
 */
typedef int (*tsig_backend_init_t)(void *ab_data, tsig_cfg_t *cfg,
                                   tsig_log_t *log);

/**
 * Pointer to audio backend output loop function.
 *
 * @param ab_data Initialized audio backend context object.
 * @param cb Sample generator callback function.
 * @param cb_data Callback function context object.
 * @return 0 if loop exited normally, negative error code upon error.
 */
typedef int (*tsig_backend_loop_t)(void *ab_data, tsig_audio_cb_t cb,
                                   void *cb_data);

/**
 * Pointer to audio backend deinitialization function.
 *
 * @param ab_data Initialized audio backend context object
 * @return 0 upon success, negative error code upon error.
 */
typedef int (*tsig_backend_deinit_t)(void *ab_data);

tsig_backend_t tsig_backend(const char *name);
const char *tsig_backend_name(tsig_backend_t backend);
