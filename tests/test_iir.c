// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * test_iir.c: Test IIR filter sine wave generator.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "iir.c"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#ifndef M_PI
#define M_PI 3.141592653589793
#endif

static const double epsilon = 0.000001;
static const double sqrt_2_2 = 0.7071067811865476;

static void test_iir_sin(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  assert_double_equal(iir_sin(-M_PI * 2), 0.0, epsilon);
  assert_double_equal(iir_sin(-M_PI * 7 / 4), sqrt_2_2, epsilon);
  assert_double_equal(iir_sin(-M_PI * 3 / 2), 1.0, epsilon);
  assert_double_equal(iir_sin(-M_PI * 5 / 4), sqrt_2_2, epsilon);
  assert_double_equal(iir_sin(-M_PI), 0.0, epsilon);
  assert_double_equal(iir_sin(-M_PI * 3 / 4), -sqrt_2_2, epsilon);
  assert_double_equal(iir_sin(-M_PI / 2), -1.0, epsilon);
  assert_double_equal(iir_sin(-M_PI / 4), -sqrt_2_2, epsilon);
  assert_double_equal(iir_sin(0.0), 0.0, epsilon);
  assert_double_equal(iir_sin(M_PI / 4), sqrt_2_2, epsilon);
  assert_double_equal(iir_sin(M_PI / 2), 1.0, epsilon);
  assert_double_equal(iir_sin(M_PI * 3 / 4), sqrt_2_2, epsilon);
  assert_double_equal(iir_sin(M_PI), 0.0, epsilon);
  assert_double_equal(iir_sin(M_PI * 5 / 4), -sqrt_2_2, epsilon);
  assert_double_equal(iir_sin(M_PI * 3 / 2), -1.0, epsilon);
  assert_double_equal(iir_sin(M_PI * 7 / 4), -sqrt_2_2, epsilon);
  assert_double_equal(iir_sin(M_PI * 2), 0.0, epsilon);
}

static void test_iir_cos(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  assert_double_equal(iir_cos(-M_PI * 2), 1.0, epsilon);
  assert_double_equal(iir_cos(-M_PI * 7 / 4), sqrt_2_2, epsilon);
  assert_double_equal(iir_cos(-M_PI * 3 / 2), 0.0, epsilon);
  assert_double_equal(iir_cos(-M_PI * 5 / 4), -sqrt_2_2, epsilon);
  assert_double_equal(iir_cos(-M_PI), -1.0, epsilon);
  assert_double_equal(iir_cos(-M_PI * 3 / 4), -sqrt_2_2, epsilon);
  assert_double_equal(iir_cos(-M_PI / 2), 0.0, epsilon);
  assert_double_equal(iir_cos(-M_PI / 4), sqrt_2_2, epsilon);
  assert_double_equal(iir_cos(0.0), 1.0, epsilon);
  assert_double_equal(iir_cos(M_PI / 4), sqrt_2_2, epsilon);
  assert_double_equal(iir_cos(M_PI / 2), 0.0, epsilon);
  assert_double_equal(iir_cos(M_PI * 3 / 4), -sqrt_2_2, epsilon);
  assert_double_equal(iir_cos(M_PI), -1.0, epsilon);
  assert_double_equal(iir_cos(M_PI * 5 / 4), -sqrt_2_2, epsilon);
  assert_double_equal(iir_cos(M_PI * 3 / 2), 0.0, epsilon);
  assert_double_equal(iir_cos(M_PI * 7 / 4), sqrt_2_2, epsilon);
  assert_double_equal(iir_cos(M_PI * 2), 1.0, epsilon);
}

static void test_iir_gcd(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  assert_int_equal(iir_gcd(0, 0), 0);
  assert_int_equal(iir_gcd(12345, 0), 12345);
  assert_int_equal(iir_gcd(2453075452, 1966396297), 1);
  assert_int_equal(iir_gcd(2436986888, 4024588454), 2);
  assert_int_equal(iir_gcd(3337804053, 2007056733), 3);
  assert_int_equal(iir_gcd(3193057840, 442176365), 5);
  assert_int_equal(iir_gcd(3312460596, 2671196406), 6);
  assert_int_equal(iir_gcd(4140985275, 179088476), 7);
  assert_int_equal(iir_gcd(2980799600, 326264890), 10);
  assert_int_equal(iir_gcd(1955320845, 1765414920), 15);
  assert_int_equal(iir_gcd(2344301729, 2939485230), 11);
  assert_int_equal(iir_gcd(3065642928, 1039149824), 16);
  assert_int_equal(iir_gcd(3826205203, 3478040147), 53);
  assert_int_equal(iir_gcd(2847460874, 1623814264), 74);
  assert_int_equal(iir_gcd(1017588278, 440604289), 113);
  assert_int_equal(iir_gcd(3220476840, 727446860), 140);
}

static void test_tsig_iir_init(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  tsig_iir_t iir;

  tsig_iir_init(&iir, 20000, 48000, 0);
  assert_int_equal(iir.freq, 20000);
  assert_int_equal(iir.rate, 48000);
  assert_int_equal(iir.phase, 0);
  assert_double_equal(iir.a, -1.7320508075688774, epsilon);
  assert_int_equal(iir.period, 12);
  assert_double_equal(iir.init_y0, 0, epsilon);
  assert_double_equal(iir.init_y1, 0.5, epsilon);

  tsig_iir_init(&iir, 16234, 343634, -634222343);
  assert_int_equal(iir.freq, 16234);
  assert_int_equal(iir.rate, 343634);
  assert_int_equal(iir.phase, -45796);
  assert_double_equal(iir.a, 1.9125363772354078, epsilon);
  assert_int_equal(iir.period, 171817);
  assert_double_equal(iir.init_y0, 0.0019198742032677954, epsilon);
  assert_double_equal(iir.init_y1, -0.29065483070271492, epsilon);
}

static void test_tsig_iir_next(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  tsig_iir_t iir;
  double tmp;

  tsig_iir_init(&iir, 20000, 48000, 0);
  assert_double_equal(tsig_iir_next(&iir), 0, epsilon);
  assert_double_equal(tsig_iir_next(&iir), 0.5, epsilon);
  tsig_iir_next(&iir);
  assert_double_equal(tsig_iir_next(&iir), 1.0, epsilon);
  tsig_iir_next(&iir);
  assert_double_equal(tsig_iir_next(&iir), 0.5, epsilon);
  assert_double_equal(tsig_iir_next(&iir), 0, epsilon);
  assert_double_equal(tsig_iir_next(&iir), -0.5, epsilon);
  tsig_iir_next(&iir);
  assert_double_equal(tsig_iir_next(&iir), -1.0, epsilon);
  tsig_iir_next(&iir);
  assert_double_equal(tsig_iir_next(&iir), -0.5, epsilon);
  assert_double_equal(tsig_iir_next(&iir), 0, epsilon);

  tsig_iir_init(&iir, 16234, 343634, -634222343);
  assert_double_equal(tsig_iir_next(&iir), 0.0019198742032677954, epsilon);
  assert_double_equal(tsig_iir_next(&iir), -0.29065483070271492, epsilon);
  tmp = tsig_iir_next(&iir);
  for (uint32_t k = 0; k < iir.period - 3; k++)
    tsig_iir_next(&iir);
  assert_double_equal(tsig_iir_next(&iir), 0.0019198742032677954, epsilon);
  assert_double_equal(tsig_iir_next(&iir), -0.29065483070271492, epsilon);
  assert_double_equal(tsig_iir_next(&iir), tmp, epsilon);
  for (uint32_t k = 0; k < iir.period - 1; k++)
    tsig_iir_next(&iir);
  assert_double_equal(tsig_iir_next(&iir), tmp, epsilon);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_iir_sin),
      cmocka_unit_test(test_iir_cos),
      cmocka_unit_test(test_iir_gcd),
      cmocka_unit_test(test_tsig_iir_init),
      cmocka_unit_test(test_tsig_iir_next),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
