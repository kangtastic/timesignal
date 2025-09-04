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
#include "defaults.h"
#include "log.h"
#include "station.h"

#ifdef TSIG_HAVE_ALSA
#include "alsa.h"
#endif /* TSIG_HAVE_ALSA */

#ifdef TSIG_HAVE_PIPEWIRE
#include "pipewire.h"
#endif /* TSIG_HAVE_PIPEWIRE */

#ifdef TSIG_HAVE_PULSE
#include "pulse.h"
#endif /* TSIG_HAVE_PULSE */

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/** Buffer size. */
#define TSIG_TIMESIGNAL_MSG_SIZE 128

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

/** Audio backends. */
static tsig_backend_info_t timesignal_backends[] = {
#ifdef TSIG_HAVE_PIPEWIRE
    [TSIG_BACKEND_PIPEWIRE] =
        {
            .backend = TSIG_BACKEND_PIPEWIRE,
            .data = &timesignal_pipewire,
            .lib_init = (tsig_backend_lib_init_t)&tsig_pipewire_lib_init,
            .init = (tsig_backend_init_t)&tsig_pipewire_init,
            .loop = (tsig_backend_loop_t)&tsig_pipewire_loop,
            .deinit = (tsig_backend_deinit_t)&tsig_pipewire_deinit,
            .lib_deinit = (tsig_backend_lib_deinit_t)&tsig_pipewire_lib_deinit,
        },
#endif /* TSIG_HAVE_PIPEWIRE */

#ifdef TSIG_HAVE_PULSE
    [TSIG_BACKEND_PULSE] =
        {
            .backend = TSIG_BACKEND_PULSE,
            .data = &timesignal_pulse,
            .lib_init = (tsig_backend_lib_init_t)&tsig_pulse_lib_init,
            .init = (tsig_backend_init_t)&tsig_pulse_init,
            .loop = (tsig_backend_loop_t)&tsig_pulse_loop,
            .deinit = (tsig_backend_deinit_t)&tsig_pulse_deinit,
            .lib_deinit = (tsig_backend_lib_deinit_t)&tsig_pulse_lib_deinit,
        },
#endif /* TSIG_HAVE_PULSE */

#ifdef TSIG_HAVE_ALSA
    [TSIG_BACKEND_ALSA] =
        {
            .backend = TSIG_BACKEND_ALSA,
            .data = &timesignal_alsa,
            .lib_init = (tsig_backend_lib_init_t)&tsig_alsa_lib_init,
            .init = (tsig_backend_init_t)&tsig_alsa_init,
            .loop = (tsig_backend_loop_t)&tsig_alsa_loop,
            .deinit = (tsig_backend_deinit_t)&tsig_alsa_deinit,
            .lib_deinit = (tsig_backend_lib_deinit_t)&tsig_alsa_lib_deinit,
        },
#endif /* TSIG_HAVE_ALSA */

    {.backend = TSIG_BACKEND_UNKNOWN},
};

/** Determine which audio backends are available. */
static void timesignal_find_backend_order(tsig_cfg_t *cfg, tsig_log_t *log) {
  tsig_backend_info_t *backend = timesignal_backends;
  char order[TSIG_TIMESIGNAL_MSG_SIZE] = {""};
  int len = 0;

#ifdef TSIG_HAVE_BACKENDS
  if (cfg->backend != TSIG_BACKEND_UNKNOWN) {
    backend[0] = timesignal_backends[cfg->backend];
    backend[1].backend = TSIG_BACKEND_UNKNOWN;
  }
#endif /* TSIG_HAVE_BACKENDS */

  for (; backend->backend != TSIG_BACKEND_UNKNOWN; backend++)
    len += sprintf(&order[len], "%s%s", len ? " " : "",
                   tsig_backend_name(backend->backend));

  tsig_log_dbg("Output method order: %s", order);
}

int main(int argc, char *argv[]) {
  tsig_backend_info_t *backend = timesignal_backends;
  tsig_station_t *station = &timesignal_station;
  tsig_cfg_t *cfg = &timesignal_cfg;
  tsig_log_t *log = &timesignal_log;
  bool is_done = false;
  int err;

  tsig_log_init(log);

  err = tsig_cfg_init(cfg, log, argc, argv);
  if (err == TSIG_CFG_INIT_FAIL)
    exit(EXIT_FAILURE);
  else if (err == TSIG_CFG_INIT_HELP)
    exit(EXIT_SUCCESS);

  tsig_log_tty("%s %s <%s>", TSIG_DEFAULTS_NAME, TSIG_DEFAULTS_VERSION,
               TSIG_DEFAULTS_URL);
  tsig_log_tty("%s", TSIG_DEFAULTS_DESCRIPTION);
  tsig_log_tty("");

  tsig_station_init(station, cfg, log);

  timesignal_find_backend_order(cfg, log);

  for (; !is_done && backend->backend != TSIG_BACKEND_UNKNOWN; backend++) {
    err = backend->lib_init(log);
    if (err < 0)
      continue;

    err = backend->init(backend->data, cfg, log);
    if (err < 0)
      goto loop_backend_lib_deinit;

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

    /* NOTE: TTY echo will not turn back on if we terminate abnormally. */
    if (log->have_status && !atexit(tsig_log_tty_enable_echo))
      tsig_log_tty_disable_echo();

    err = backend->loop(backend->data, tsig_station_cb, (void *)station);
    if (err == SIGINT)
      tsig_log_note("Exiting on interrupt.");
    else if (err == SIGALRM)
      tsig_log("Exiting as scheduled.");
    else if (err == SIGTERM)
      tsig_log_warn("Exiting on SIGTERM!");
    else if (err < 0)
      tsig_log_err("Failed to cleanly exit output loop!");

    is_done = true;

    backend->deinit(backend->data);

  loop_backend_lib_deinit:
    backend->lib_deinit(log);
  }

  if (!is_done) {
    tsig_log_err("Failed to find a suitable audio backend!");
    exit(EXIT_FAILURE);
  }

  tsig_log_deinit(log);

  exit(EXIT_SUCCESS);
}
