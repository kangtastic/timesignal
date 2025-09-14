// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * test_util.c: Test miscellaneous utilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "util.c"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#include <cmocka.h>

static void test_tsig_util_getprogname(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  char progname[PATH_MAX];
  tsig_util_getprogname(progname);
  assert_string_equal(progname, "test_util");
}

static void test_tsig_util_strcasecmp(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  int rv;
  rv = tsig_util_strcasecmp("Ab", "Ab");
  assert_int_equal(rv, 0);
  rv = tsig_util_strcasecmp("Abc", "abC");
  assert_int_equal(rv, 0);
  rv = tsig_util_strcasecmp("", "");
  assert_int_equal(rv, 0);
  rv = tsig_util_strcasecmp("Abc", "a");
  assert_int_not_equal(rv, 0);
  assert_int_not_equal(rv, -1);
  rv = tsig_util_strcasecmp("Abc", "");
  assert_int_not_equal(rv, 0);
  assert_int_not_equal(rv, -1);
  rv = tsig_util_strcasecmp("Abc", NULL);
  assert_int_equal(rv, -1);
  rv = tsig_util_strcasecmp(NULL, "");
  assert_int_equal(rv, -1);
  rv = tsig_util_strcasecmp(NULL, NULL);
  assert_int_equal(rv, -1);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_tsig_util_getprogname),
      cmocka_unit_test(test_tsig_util_strcasecmp),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
