// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * timesignal.c: Entry point.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "alsa.h"
#include "cfg.h"
#include "log.h"
#include "station.h"

#include <stdlib.h>

int main(int argc, char *argv[]) {
  tsig_station_t station;
  tsig_alsa_t alsa;
  tsig_cfg_t cfg;
  tsig_log_t log;
  int err;

  tsig_log_init(&log);

  err = tsig_cfg_init(&cfg, &log, argc, argv);
  if (err == TSIG_CFG_INIT_FAIL)
    exit(EXIT_FAILURE);
  else if (err == TSIG_CFG_INIT_HELP)
    exit(EXIT_SUCCESS);

  err = tsig_alsa_init(&alsa, &cfg, &log);
  if (err < 0)
    exit(EXIT_FAILURE);

  tsig_station_init(&station, &log, cfg.station, cfg.offset, cfg.dut1,
                    cfg.smooth, cfg.ultrasound, alsa.rate);

  err = tsig_alsa_loop(&alsa, tsig_station_cb, (void *)&station);
  if (err < 0)
    exit(EXIT_FAILURE);

  tsig_alsa_deinit(&alsa);

  tsig_log_deinit(&log);

  exit(EXIT_SUCCESS);
}
