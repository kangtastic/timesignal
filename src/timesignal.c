// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * timesignal.c: Entry point.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "backend.h"
#include "cfg.h"
#include "log.h"
#include "station.h"

#ifdef TSIG_HAVE_PIPEWIRE
#include "pipewire.h"
#endif /* TSIG_HAVE_PIPEWIRE */

#ifdef TSIG_HAVE_PULSE
#include "pulse.h"
#endif /* TSIG_HAVE_PULSE */

#ifdef TSIG_HAVE_ALSA
#include "alsa.h"
#endif /* TSIG_HAVE_ALSA */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* Module globals. */
#ifdef TSIG_HAVE_PIPEWIRE
static tsig_pipewire_t timesignal_pipewire;
#endif /* TSIG_HAVE_PIPEWIRE */

#ifdef TSIG_HAVE_PULSE
static tsig_pulse_t timesignal_pulse;
#endif /* TSIG_HAVE_PULSE */

#ifdef TSIG_HAVE_ALSA
static tsig_alsa_t timesignal_alsa;
#endif /* TSIG_HAVE_ALSA */

static tsig_station_t timesignal_station;
static tsig_cfg_t timesignal_cfg;
static tsig_log_t timesignal_log;

/** Audio backend information. */
typedef struct timesignal_backend_info {
  tsig_backend_t backend;
  void *data;
  tsig_backend_init_t init;
  tsig_backend_loop_t loop;
  tsig_backend_deinit_t deinit;
} timesignal_backend_t;

/** Audio backends. */
static timesignal_backend_t timesignal_backends[] = {
#ifdef TSIG_HAVE_PIPEWIRE
    [TSIG_BACKEND_PIPEWIRE] =
        {
            .backend = TSIG_BACKEND_PIPEWIRE,
            .data = &timesignal_pipewire,
            .init = (tsig_backend_init_t)&tsig_pipewire_init,
            .loop = (tsig_backend_loop_t)&tsig_pipewire_loop,
            .deinit = (tsig_backend_deinit_t)&tsig_pipewire_deinit,
        },
#endif /* TSIG_HAVE_PIPEWIRE */

#ifdef TSIG_HAVE_PULSE
    [TSIG_BACKEND_PULSE] =
        {
            .backend = TSIG_BACKEND_PULSE,
            .data = &timesignal_pulse,
            .init = (tsig_backend_init_t)&tsig_pulse_init,
            .loop = (tsig_backend_loop_t)&tsig_pulse_loop,
            .deinit = (tsig_backend_deinit_t)&tsig_pulse_deinit,
        },
#endif /* TSIG_HAVE_PULSE */

#ifdef TSIG_HAVE_ALSA
    [TSIG_BACKEND_ALSA] =
        {
            .backend = TSIG_BACKEND_ALSA,
            .data = &timesignal_alsa,
            .init = (tsig_backend_init_t)&tsig_alsa_init,
            .loop = (tsig_backend_loop_t)&tsig_alsa_loop,
            .deinit = (tsig_backend_deinit_t)&tsig_alsa_deinit,
        },
#endif /* TSIG_HAVE_ALSA */
};

int main(int argc, char *argv[]) {
  int backends = sizeof(timesignal_backends) / sizeof(timesignal_backends[0]);
  tsig_station_t *station = &timesignal_station;
  tsig_cfg_t *cfg = &timesignal_cfg;
  tsig_log_t *log = &timesignal_log;
  timesignal_backend_t *backend;
  bool is_done = false;
  const char *name;
  int err;

  tsig_log_init(log);

  err = tsig_cfg_init(cfg, log, argc, argv);
  if (err == TSIG_CFG_INIT_FAIL)
    exit(EXIT_FAILURE);
  else if (err == TSIG_CFG_INIT_HELP)
    exit(EXIT_SUCCESS);

  tsig_station_init(station, cfg, log);

#ifdef TSIG_HAVE_BACKENDS
  if (cfg->backend != TSIG_BACKEND_UNKNOWN) {
    timesignal_backends[0] = timesignal_backends[cfg->backend];
    backends = 1;
  }
#endif /* TSIG_HAVE_BACKENDS */

  for (int i = 0; i < backends; i++) {
    backend = &timesignal_backends[i];
    name = tsig_backend_name(backend->backend);

    tsig_log("Trying %s backend.", name);

    err = backend->init(backend->data, cfg, log);
    if (err < 0)
      continue;

#ifdef TSIG_HAVE_PULSE
    /* PulseAudio may not support the configured rate. */
    if (backend->backend == TSIG_BACKEND_PULSE)
      tsig_station_set_rate(station, timesignal_pulse.rate);
#endif /* TSIG_HAVE_ALSA */

#ifdef TSIG_HAVE_ALSA
    /* ALSA may not have given us the rate we requested. */
    if (backend->backend == TSIG_BACKEND_ALSA)
      tsig_station_set_rate(station, timesignal_alsa.rate);
#endif /* TSIG_HAVE_ALSA */

    err = backend->loop(backend->data, tsig_station_cb, (void *)station);
    if (err < 0)
      tsig_log_err("failed to cleanly exit output loop");

    backend->deinit(backend->data);

    is_done = true;

    break;
  }

  if (!is_done) {
    tsig_log_err("failed to find a suitable audio backend\n");
    exit(EXIT_FAILURE);
  }

  tsig_log_deinit(log);

  exit(EXIT_SUCCESS);
}
