/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * iir.h: Header for IIR filter sine wave generator.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#pragma once

#include <stdint.h>

/** IIR filter sine wave generator. */
typedef struct tsig_iir {
  uint32_t freq; /** Sine wave frequency in Hz. */
  uint32_t rate; /** Sample rate in Hz. */
  int phase;     /** Initial phase offset in samples. */

  double a;        /** Filter coefficient A. */
  uint32_t period; /** Period of generator in samples. */
  double init_y0;  /** First sample value. */
  double init_y1;  /** Second sample value. */

  uint32_t sample; /** Current sample number in period. */
  double y0;       /** Current sample value. */
  double y1;       /** Next sample value. */
} tsig_iir_t;

void tsig_iir_init(tsig_iir_t *, uint32_t, uint32_t, int);
double tsig_iir_next(tsig_iir_t *);
