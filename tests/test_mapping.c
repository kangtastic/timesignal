// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * test_mapping.c: Test key-value mappings.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "mapping.c"

#include "util.c"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

static tsig_mapping_t test_mapping[] = {
    {"Foo", 0},
    {"Bar", 1},
    {NULL, 0},
};

static tsig_mapping_nn_t test_mapping_nn[] = {
    {0, 1},
    {2, 3},
    {0, 0},
};

static void test_tsig_mapping_match_key(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  assert_int_equal(tsig_mapping_match_key(test_mapping, "Foo"), 0);
  assert_int_equal(tsig_mapping_match_key(test_mapping, "FoO"), 0);
  assert_int_equal(tsig_mapping_match_key(test_mapping, "Bar"), 1);
  assert_int_equal(tsig_mapping_match_key(test_mapping, "BaR"), 1);
  assert_int_equal(tsig_mapping_match_key(test_mapping, "Baz"), -1);
  assert_int_equal(tsig_mapping_match_key(test_mapping, NULL), -1);
  assert_int_equal(tsig_mapping_match_key(test_mapping, ""), -1);
}

static void test_tsig_mapping_match_value(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  assert_string_equal(tsig_mapping_match_value(test_mapping, 0), "Foo");
  assert_string_equal(tsig_mapping_match_value(test_mapping, 1), "Bar");
  assert_int_equal(tsig_mapping_match_value(test_mapping, 2), NULL);
}

static void test_tsig_mapping_nn_match_key(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  assert_int_equal(tsig_mapping_nn_match_key(test_mapping_nn, 0), 1);
  assert_int_equal(tsig_mapping_nn_match_key(test_mapping_nn, 2), 3);
  assert_int_equal(tsig_mapping_nn_match_key(test_mapping_nn, 3), -1);
}

static void test_tsig_mapping_nn_match_value(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  assert_int_equal(tsig_mapping_nn_match_value(test_mapping_nn, 1), 0);
  assert_int_equal(tsig_mapping_nn_match_value(test_mapping_nn, 3), 2);
  assert_int_equal(tsig_mapping_nn_match_value(test_mapping_nn, 0), -1);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_tsig_mapping_match_key),
      cmocka_unit_test(test_tsig_mapping_match_value),
      cmocka_unit_test(test_tsig_mapping_nn_match_key),
      cmocka_unit_test(test_tsig_mapping_nn_match_value),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
