// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * iir.c: IIR filter sine wave generator.
 *
 * This file is part of timesignal.
 *
 * Based on a 2nd-order infinite impulse response (IIR) filter
 * like that used by the TI TMS320C62x DSP for sine generation [1].
 *
 * Briefly, to obtain the next sample Y[n] at sample rate R for a sine wave
 * with frequency F from the two previous sample values Y[n - 2] and Y[n - 1]:
 *
 *   Y[n] = A * Y[n - 2] - Y[n - 1]
 *
 * where A is a constant filter coefficient given by:
 *
 *   A = 2 * cos(2 * pi * F / R)
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 *
 * [1]: Y. Cheng, "TMS320C62x Algorithm: Sine Wave Generation",
 *      Application Note SPRA708, Texas Instruments (2000). [Online].
 *      Available: https://www.ti.com.cn/cn/lit/an/spra708/spra708.pdf
 */

#include "iir.h"

#include <stdint.h>

#ifdef TSIG_USE_C_MATH
#include <math.h>
#define iir_sin(x) sin((x))
#define iir_cos(x) cos((x))
#else
#include <float.h>
#if DBL_MANT_DIG != 53
#error size of double is not 64 bits
#endif /* DBL_MANT_DIG */

/***************************** BEGIN MATH ROUTINES *****************************
 *
 * Routines for computing sine and cosine by Taylor polynomials of degree 13.
 *
 * Originally from FreeBSD's C library:
 *
 *   lib/msun/src/math_private.h
 *   lib/msun/src/k_cos.c
 *   lib/msun/src/k_sin.c
 *   lib/msun/src/s_cos.c
 *   lib/msun/src/s_sin.c
 *
 * Huge swaths of code consisting of esoteric performance optimizations
 * and facilities for handling large-magnitude inputs have been removed,
 * as they are unnecessary for our restricted use case.
 *
 * The following license applies to these math routines:
 *
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */

static const double iir_c1 = 4.16666666666666019037e-02;
static const double iir_c2 = -1.38888888888741095749e-03;
static const double iir_c3 = 2.48015872894767294178e-05;
static const double iir_c4 = -2.75573143513906633035e-07;
static const double iir_c5 = 2.08757232129817482790e-09;
static const double iir_c6 = -1.13596475577881948265e-11;

static const double iir_s1 = -1.66666666666666324348e-01;
static const double iir_s2 = 8.33333333332248946124e-03;
static const double iir_s3 = -1.98412698298579493134e-04;
static const double iir_s4 = 2.75573137070700676789e-06;
static const double iir_s5 = -2.50507602534068634195e-08;
static const double iir_s6 = 1.58969099521155010221e-10;

static const double iir_pi_4 = 7.853981633974482789995e-01;
static const double iir_pi_2 = 1.570796326794896557999e+00;
static const double iir_3pi_4 = 2.356194490192344836998e+00;
static const double iir_pi = 3.141592653589793115998e+00;
static const double iir_2pi = 6.283185307179586231996e+00;

static uint32_t iir_double_u32_hi(double x) {
  union {
    uint64_t u64;
    double f64;
  } n = {.f64 = x};
  return n.u64 >> 32;
}

static double iir_k_sin(double x) {
  double z = x * x;
  double w = z * z;
  double r = iir_s2 + z * (iir_s3 + z * iir_s4) + z * w * (iir_s5 + z * iir_s6);
  return x + z * x * (iir_s1 + z * r);
}

static double iir_k_cos(double x) {
  double z = x * x;
  double w = z * z;
  double r = z * (iir_c1 + z * (iir_c2 + z * iir_c3)) +
             w * w * (iir_c4 + z * (iir_c5 + z * iir_c6));
  double hz = 0.5 * z;
  w = 1.0 - hz;
  return w + (((1.0 - w) - hz) + z * r);
}

double iir_sin(double x) {
  /* High word of x. */
  int32_t ix = (int32_t)iir_double_u32_hi(x);

  /* |x| ~< pi/4 */
  ix &= 0x7fffffff;
  if (ix <= 0x3fe921fb) {
    if (ix < 0x3e500000) /* |x| < 2**-26 */
      if ((int)x == 0)   /* generate inexact */
        return x;
    return iir_k_sin(x);
  }

  /* sin(Inf or NaN) is NaN */
  if (ix >= 0x7ff00000)
    return x - x;

  /* argument reduction needed */
  while (x < -iir_pi)
    x += iir_2pi;
  while (x > iir_pi)
    x -= iir_2pi;

  return (x < -iir_3pi_4)  ? -iir_k_sin(x + iir_pi)
         : (x < -iir_pi_4) ? -iir_k_cos(x + iir_pi_2)
         : (x < iir_pi_4)  ? iir_k_sin(x)
         : (x < iir_3pi_4) ? iir_k_cos(x - iir_pi_2)
                           : -iir_k_sin(x - iir_pi);
}

double iir_cos(double x) {
  /* High word of x. */
  int32_t ix = (int32_t)iir_double_u32_hi(x);

  /* |x| ~< pi/4 */
  ix &= 0x7fffffff;
  if (ix <= 0x3fe921fb) {
    if (ix < 0x3e46a09e) /* if x < 2**-27 * sqrt(2) */
      if (((int)x) == 0) /* generate inexact */
        return 1.0;
    return iir_k_cos(x);
  }

  /* cos(Inf or NaN) is NaN */
  if (ix >= 0x7ff00000)
    return x - x;

  /* argument reduction needed */
  while (x < -iir_pi)
    x += iir_2pi;
  while (x > iir_pi)
    x -= iir_2pi;

  return (x < -iir_3pi_4)  ? -iir_k_cos(x + iir_pi)
         : (x < -iir_pi_4) ? iir_k_sin(x + iir_pi_2)
         : (x < iir_pi_4)  ? iir_k_cos(x)
         : (x < iir_3pi_4) ? -iir_k_sin(x - iir_pi_2)
                           : -iir_k_cos(x - iir_pi);
}
/**************************** END MATH ROUTINES *****************************/
#endif /* TSIG_USE_C_MATH */

static uint32_t iir_gcd(uint32_t a, uint32_t b) {
  for (uint32_t c; b; a = b, b = c)
    c = a % b;
  return a;
}

/**
 * Initialize an IIR filter sine wave generator.
 *
 * @param iir: Pointer to an IIR filter sine wave generator.
 * @param freq: Sine wave frequency in Hz.
 * @param rate: Sample rate in Hz.
 * @param phase: Initial phase offset in samples.
 */
void tsig_iir_init(tsig_iir_t *iir, uint32_t freq, uint32_t rate, int phase) {
  int phase_delta;
  int phase_base;
  double angle;
  uint32_t gcd;

  iir->freq = freq;
  iir->rate = rate;

  /*
   * Compute the phase change per sample as a fraction of 2*pi.
   * The denominator of this fraction also happens to be the period.
   */
  gcd = iir_gcd(freq, rate);
  phase_delta = freq / gcd;
  phase_base = rate / gcd;
  iir->period = phase_base;

  /* Compute A as twice the cosine of the phase change per sample. */
  angle = iir_2pi * phase_delta / phase_base;
  iir->a = 2.0 * iir_cos(angle);

  /* Normalize the initial sample offset to fall within (-period, period). */
  phase %= phase_base;
  iir->phase = phase;

  /*
   * Compute the initial phase as a fraction of 2*pi. Note that `phase`
   * now signifies the numerator of this fraction, not a sample count.
   */
  phase = ((int64_t)phase * phase_delta) % phase_base;

  /* Prime the generator with the first two samples. */
  angle = iir_2pi * phase / phase_base;
  iir->init_y0 = iir_sin(angle);

  phase += phase_delta;
  if (phase >= phase_base)
    phase -= phase_base;

  angle = iir_2pi * phase / phase_base;
  iir->init_y1 = iir_sin(angle);

  /* Set the generator to the start of its period. */
  iir->sample = 0;
}

/**
 * Generate a sample from an IIR filter sine wave generator.
 *
 * @param iir: Pointer to an initialized IIR filter sine wave generator.
 * @return Sample value.
 */
double tsig_iir_next(tsig_iir_t *iir) {
  double next_y;
  double ret;

  /*
   * Reset generator state at the start of each period to eliminate
   * accumulated floating-point error from repeated sample generation.
   */
  if (!iir->sample) {
    iir->y0 = iir->init_y0;
    iir->y1 = iir->init_y1;
  }

  ret = iir->y0;

  /* Generate the next sample unless a reset is imminent. */
  if (iir->sample + 2 < iir->period) {
    next_y = iir->a * iir->y1 - iir->y0;
    iir->y0 = iir->y1;
    iir->y1 = next_y;
    iir->sample++;
  } else if (iir->sample + 1 < iir->period) {
    iir->y0 = iir->y1;
    iir->sample++;
  } else {
    iir->sample = 0;
  }

  return ret;
}
