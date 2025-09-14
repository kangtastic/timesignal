// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * backend.c: Test audio backend facilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "backend.c"

#include "mapping.c"
#include "util.c"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

static void test_tsig_backend(void **state) {
  (void)state; /* Suppress unused parameter warning. */

#ifdef TSIG_HAVE_PIPEWIRE
  assert_int_equal(tsig_backend("PipeWire"), TSIG_BACKEND_PIPEWIRE);
  assert_int_equal(tsig_backend("PiPeWiRe"), TSIG_BACKEND_PIPEWIRE);
  assert_int_equal(tsig_backend("pw"), TSIG_BACKEND_PIPEWIRE);
  assert_int_equal(tsig_backend("Pw"), TSIG_BACKEND_PIPEWIRE);
#endif /* TSIG_HAVE_PIPEWIRE */

#ifdef TSIG_HAVE_PULSE
  assert_int_equal(tsig_backend("PulseAudio"), TSIG_BACKEND_PULSE);
  assert_int_equal(tsig_backend("PuLsEaUdIo"), TSIG_BACKEND_PULSE);
  assert_int_equal(tsig_backend("Pulse"), TSIG_BACKEND_PULSE);
  assert_int_equal(tsig_backend("PuLsE"), TSIG_BACKEND_PULSE);
  assert_int_equal(tsig_backend("pa"), TSIG_BACKEND_PULSE);
  assert_int_equal(tsig_backend("Pa"), TSIG_BACKEND_PULSE);
#endif /* TSIG_HAVE_PULSE */

#ifdef TSIG_HAVE_ALSA
  assert_int_equal(tsig_backend("ALSA"), TSIG_BACKEND_ALSA);
  assert_int_equal(tsig_backend("AlSa"), TSIG_BACKEND_ALSA);
#endif /* TSIG_HAVE_ALSA */

  assert_int_equal(tsig_backend(""), TSIG_BACKEND_UNKNOWN);
  assert_int_equal(tsig_backend(NULL), TSIG_BACKEND_UNKNOWN);
  assert_int_equal(tsig_backend("asdf"), TSIG_BACKEND_UNKNOWN);
}

static void test_tsig_backend_name(void **state) {
  (void)state; /* Suppress unused parameter warning. */

#ifdef TSIG_HAVE_PIPEWIRE
  assert_string_equal(tsig_backend_name(TSIG_BACKEND_PIPEWIRE), "PipeWire");
#endif /* TSIG_HAVE_PIPEWIRE */

#ifdef TSIG_HAVE_PULSE
  assert_string_equal(tsig_backend_name(TSIG_BACKEND_PULSE), "PulseAudio");
#endif /* TSIG_HAVE_PULSE */

#ifdef TSIG_HAVE_ALSA
  assert_string_equal(tsig_backend_name(TSIG_BACKEND_ALSA), "ALSA");
#endif /* TSIG_HAVE_ALSA */
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_tsig_backend),
      cmocka_unit_test(test_tsig_backend_name),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
