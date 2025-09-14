// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * test_datetime.c: Test date and time facilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "datetime.c"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

static void test_tsig_datetime_get_timestamp(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  assert_int_not_equal(tsig_datetime_get_timestamp(), 0);
}

static void test_tsig_datetime_parse_timestamp(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  int64_t timestamp = 4102403696789;
  tsig_datetime_t datetime = tsig_datetime_parse_timestamp(timestamp);
  assert_int_equal(datetime.timestamp, timestamp);
  assert_int_equal(datetime.year, 2099);
  assert_int_equal(datetime.mon, 12);
  assert_int_equal(datetime.day, 31);
  assert_int_equal(datetime.doy, 365);
  assert_int_equal(datetime.dow, 4);
  assert_int_equal(datetime.hour, 12);
  assert_int_equal(datetime.min, 34);
  assert_int_equal(datetime.sec, 56);
  assert_int_equal(datetime.msec, 789);
}

static void test_tsig_datetime_make_timestamp(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  int64_t timestamp;
  timestamp = tsig_datetime_make_timestamp(1969, 12, 31, 23, 59, 59, 999, 0);
  assert_int_equal(timestamp, 0);
  timestamp = tsig_datetime_make_timestamp(1970, 1, 1, 0, 0, 0, 0, 0);
  assert_int_equal(timestamp, 0);
  timestamp = tsig_datetime_make_timestamp(1970, 1, 1, 0, 0, 0, 0, -480);
  assert_int_equal(timestamp, 28800000);
  timestamp = tsig_datetime_make_timestamp(2099, 12, 31, 12, 34, 56, 789, 0);
  assert_int_equal(timestamp, 4102403696789);
}

static void test_tsig_datetime_is_leap(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  assert_true(tsig_datetime_is_leap(1996));
  assert_false(tsig_datetime_is_leap(1997));
  assert_false(tsig_datetime_is_leap(1998));
  assert_false(tsig_datetime_is_leap(1999));
  assert_true(tsig_datetime_is_leap(2000));
  assert_true(tsig_datetime_is_leap(2004));
  assert_true(tsig_datetime_is_leap(2020));
  assert_true(tsig_datetime_is_leap(2024));
  assert_false(tsig_datetime_is_leap(2025));
  assert_false(tsig_datetime_is_leap(2100));
  assert_false(tsig_datetime_is_leap(2200));
  assert_false(tsig_datetime_is_leap(2300));
  assert_true(tsig_datetime_is_leap(2400));
}

static void test_tsig_datetime_days_in_mon(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  assert_int_equal(tsig_datetime_days_in_mon(1999, 1), 31);
  assert_int_equal(tsig_datetime_days_in_mon(1999, 2), 28);
  assert_int_equal(tsig_datetime_days_in_mon(1999, 3), 31);
  assert_int_equal(tsig_datetime_days_in_mon(1999, 4), 30);
  assert_int_equal(tsig_datetime_days_in_mon(1999, 5), 31);
  assert_int_equal(tsig_datetime_days_in_mon(1999, 6), 30);
  assert_int_equal(tsig_datetime_days_in_mon(1999, 7), 31);
  assert_int_equal(tsig_datetime_days_in_mon(1999, 8), 31);
  assert_int_equal(tsig_datetime_days_in_mon(1999, 9), 30);
  assert_int_equal(tsig_datetime_days_in_mon(1999, 10), 31);
  assert_int_equal(tsig_datetime_days_in_mon(1999, 11), 30);
  assert_int_equal(tsig_datetime_days_in_mon(1999, 12), 31);
  assert_int_equal(tsig_datetime_days_in_mon(2000, 1), 31);
  assert_int_equal(tsig_datetime_days_in_mon(2000, 2), 29);
  assert_int_equal(tsig_datetime_days_in_mon(2000, 3), 31);
  assert_int_equal(tsig_datetime_days_in_mon(2000, 4), 30);
  assert_int_equal(tsig_datetime_days_in_mon(2000, 5), 31);
  assert_int_equal(tsig_datetime_days_in_mon(2000, 6), 30);
  assert_int_equal(tsig_datetime_days_in_mon(2000, 7), 31);
  assert_int_equal(tsig_datetime_days_in_mon(2000, 8), 31);
  assert_int_equal(tsig_datetime_days_in_mon(2000, 9), 30);
  assert_int_equal(tsig_datetime_days_in_mon(2000, 10), 31);
  assert_int_equal(tsig_datetime_days_in_mon(2000, 11), 30);
  assert_int_equal(tsig_datetime_days_in_mon(2000, 12), 31);
}

static void test_tsig_datetime_is_eu_dst(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  tsig_datetime_t datetime;
  int32_t in_mins = 12345;

  datetime = tsig_datetime_parse_timestamp(4070908800000);
  assert_false(tsig_datetime_is_eu_dst(datetime, NULL));
  assert_int_equal(in_mins, 12345);
  datetime = tsig_datetime_parse_timestamp(4076006399999);
  assert_false(tsig_datetime_is_eu_dst(datetime, &in_mins));
  assert_int_equal(in_mins, -1);
  datetime = tsig_datetime_parse_timestamp(4076006400000);
  assert_false(tsig_datetime_is_eu_dst(datetime, &in_mins));
  assert_int_equal(in_mins, -1);
  datetime = tsig_datetime_parse_timestamp(4078339199999);
  assert_false(tsig_datetime_is_eu_dst(datetime, &in_mins));
  assert_int_equal(in_mins, -1);
  datetime = tsig_datetime_parse_timestamp(4078339200000);
  assert_false(tsig_datetime_is_eu_dst(datetime, &in_mins));
  assert_int_equal(in_mins, 1500);
  datetime = tsig_datetime_parse_timestamp(4078429139999);
  assert_false(tsig_datetime_is_eu_dst(datetime, &in_mins));
  assert_int_equal(in_mins, 2);
  datetime = tsig_datetime_parse_timestamp(4078429199999);
  assert_false(tsig_datetime_is_eu_dst(datetime, &in_mins));
  assert_int_equal(in_mins, 1);
  datetime = tsig_datetime_parse_timestamp(4078429200000);
  assert_true(tsig_datetime_is_eu_dst(datetime, &in_mins));
  assert_int_equal(in_mins, -1);
  datetime = tsig_datetime_parse_timestamp(4094495999999);
  assert_true(tsig_datetime_is_eu_dst(datetime, &in_mins));
  assert_int_equal(in_mins, -1);
  datetime = tsig_datetime_parse_timestamp(4094496000000);
  assert_true(tsig_datetime_is_eu_dst(datetime, &in_mins));
  assert_int_equal(in_mins, -1);
  datetime = tsig_datetime_parse_timestamp(4094496000000);
  assert_true(tsig_datetime_is_eu_dst(datetime, &in_mins));
  assert_int_equal(in_mins, -1);
  datetime = tsig_datetime_parse_timestamp(4096483199999);
  assert_true(tsig_datetime_is_eu_dst(datetime, &in_mins));
  assert_int_equal(in_mins, -1);
  datetime = tsig_datetime_parse_timestamp(4096483200000);
  assert_true(tsig_datetime_is_eu_dst(datetime, &in_mins));
  assert_int_equal(in_mins, 1500);
  datetime = tsig_datetime_parse_timestamp(4096573139999);
  assert_true(tsig_datetime_is_eu_dst(datetime, &in_mins));
  assert_int_equal(in_mins, 2);
  datetime = tsig_datetime_parse_timestamp(4096573199999);
  assert_true(tsig_datetime_is_eu_dst(datetime, &in_mins));
  assert_int_equal(in_mins, 1);
  datetime = tsig_datetime_parse_timestamp(4096573200000);
  assert_false(tsig_datetime_is_eu_dst(datetime, &in_mins));
  assert_int_equal(in_mins, -1);
  datetime = tsig_datetime_parse_timestamp(4097174399999);
  assert_false(tsig_datetime_is_eu_dst(datetime, &in_mins));
  assert_int_equal(in_mins, -1);
  datetime = tsig_datetime_parse_timestamp(4097174400000);
  assert_false(tsig_datetime_is_eu_dst(datetime, &in_mins));
  assert_int_equal(in_mins, -1);
}

static void test_tsig_datetime_is_us_dst(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  tsig_datetime_t datetime;
  bool is_dst_end = true;

  datetime = tsig_datetime_parse_timestamp(4102444800000);
  assert_false(tsig_datetime_is_us_dst(datetime, NULL));
  assert_true(is_dst_end);
  datetime = tsig_datetime_parse_timestamp(4107542399999);
  assert_false(tsig_datetime_is_us_dst(datetime, &is_dst_end));
  assert_false(is_dst_end);
  datetime = tsig_datetime_parse_timestamp(4107542400000);
  assert_false(tsig_datetime_is_us_dst(datetime, &is_dst_end));
  assert_false(is_dst_end);
  datetime = tsig_datetime_parse_timestamp(4108665599999);
  assert_false(tsig_datetime_is_us_dst(datetime, &is_dst_end));
  assert_false(is_dst_end);
  datetime = tsig_datetime_parse_timestamp(4108665600000);
  assert_false(tsig_datetime_is_us_dst(datetime, &is_dst_end));
  assert_true(is_dst_end);
  datetime = tsig_datetime_parse_timestamp(4108751999999);
  assert_false(tsig_datetime_is_us_dst(datetime, &is_dst_end));
  assert_true(is_dst_end);
  datetime = tsig_datetime_parse_timestamp(4108752000000);
  assert_true(tsig_datetime_is_us_dst(datetime, &is_dst_end));
  assert_true(is_dst_end);
  datetime = tsig_datetime_parse_timestamp(4129228799999);
  assert_true(tsig_datetime_is_us_dst(datetime, &is_dst_end));
  assert_true(is_dst_end);
  datetime = tsig_datetime_parse_timestamp(4129228800000);
  assert_true(tsig_datetime_is_us_dst(datetime, &is_dst_end));
  assert_false(is_dst_end);
  datetime = tsig_datetime_parse_timestamp(4129315199999);
  assert_true(tsig_datetime_is_us_dst(datetime, &is_dst_end));
  assert_false(is_dst_end);
  datetime = tsig_datetime_parse_timestamp(4129315200000);
  assert_false(tsig_datetime_is_us_dst(datetime, &is_dst_end));
  assert_false(is_dst_end);
  datetime = tsig_datetime_parse_timestamp(4131302400000);
  assert_false(tsig_datetime_is_us_dst(datetime, &is_dst_end));
  assert_false(is_dst_end);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_tsig_datetime_get_timestamp),
      cmocka_unit_test(test_tsig_datetime_parse_timestamp),
      cmocka_unit_test(test_tsig_datetime_make_timestamp),
      cmocka_unit_test(test_tsig_datetime_is_leap),
      cmocka_unit_test(test_tsig_datetime_days_in_mon),
      cmocka_unit_test(test_tsig_datetime_is_eu_dst),
      cmocka_unit_test(test_tsig_datetime_is_us_dst),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
