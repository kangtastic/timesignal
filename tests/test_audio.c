// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * test_audio.c: Test generalized audio facilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "audio.c"

#include "mapping.c"
#include "util.c"

#include <endian.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

static void test_audio_format_is_float(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  assert_false(audio_format_is_float(TSIG_AUDIO_FORMAT_UNKNOWN));
  assert_false(audio_format_is_float(TSIG_AUDIO_FORMAT_S16));
  assert_false(audio_format_is_float(TSIG_AUDIO_FORMAT_S16_LE));
  assert_false(audio_format_is_float(TSIG_AUDIO_FORMAT_S16_BE));
  assert_false(audio_format_is_float(TSIG_AUDIO_FORMAT_S24));
  assert_false(audio_format_is_float(TSIG_AUDIO_FORMAT_S24_LE));
  assert_false(audio_format_is_float(TSIG_AUDIO_FORMAT_S24_BE));
  assert_false(audio_format_is_float(TSIG_AUDIO_FORMAT_S32));
  assert_false(audio_format_is_float(TSIG_AUDIO_FORMAT_S32_LE));
  assert_false(audio_format_is_float(TSIG_AUDIO_FORMAT_S32_BE));
  assert_false(audio_format_is_float(TSIG_AUDIO_FORMAT_U16));
  assert_false(audio_format_is_float(TSIG_AUDIO_FORMAT_U16_LE));
  assert_false(audio_format_is_float(TSIG_AUDIO_FORMAT_U16_BE));
  assert_false(audio_format_is_float(TSIG_AUDIO_FORMAT_U24));
  assert_false(audio_format_is_float(TSIG_AUDIO_FORMAT_U24_LE));
  assert_false(audio_format_is_float(TSIG_AUDIO_FORMAT_U24_BE));
  assert_false(audio_format_is_float(TSIG_AUDIO_FORMAT_U32));
  assert_false(audio_format_is_float(TSIG_AUDIO_FORMAT_U32_LE));
  assert_false(audio_format_is_float(TSIG_AUDIO_FORMAT_U32_BE));
  assert_true(audio_format_is_float(TSIG_AUDIO_FORMAT_FLOAT));
  assert_true(audio_format_is_float(TSIG_AUDIO_FORMAT_FLOAT_LE));
  assert_true(audio_format_is_float(TSIG_AUDIO_FORMAT_FLOAT_BE));
  assert_true(audio_format_is_float(TSIG_AUDIO_FORMAT_FLOAT64));
  assert_true(audio_format_is_float(TSIG_AUDIO_FORMAT_FLOAT64_LE));
  assert_true(audio_format_is_float(TSIG_AUDIO_FORMAT_FLOAT64_BE));
}

static void test_audio_format_is_signed(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  assert_false(audio_format_is_signed(TSIG_AUDIO_FORMAT_UNKNOWN));
  assert_true(audio_format_is_signed(TSIG_AUDIO_FORMAT_S16));
  assert_true(audio_format_is_signed(TSIG_AUDIO_FORMAT_S16_LE));
  assert_true(audio_format_is_signed(TSIG_AUDIO_FORMAT_S16_BE));
  assert_true(audio_format_is_signed(TSIG_AUDIO_FORMAT_S24));
  assert_true(audio_format_is_signed(TSIG_AUDIO_FORMAT_S24_LE));
  assert_true(audio_format_is_signed(TSIG_AUDIO_FORMAT_S24_BE));
  assert_true(audio_format_is_signed(TSIG_AUDIO_FORMAT_S32));
  assert_true(audio_format_is_signed(TSIG_AUDIO_FORMAT_S32_LE));
  assert_true(audio_format_is_signed(TSIG_AUDIO_FORMAT_S32_BE));
  assert_false(audio_format_is_signed(TSIG_AUDIO_FORMAT_U16));
  assert_false(audio_format_is_signed(TSIG_AUDIO_FORMAT_U16_LE));
  assert_false(audio_format_is_signed(TSIG_AUDIO_FORMAT_U16_BE));
  assert_false(audio_format_is_signed(TSIG_AUDIO_FORMAT_U24));
  assert_false(audio_format_is_signed(TSIG_AUDIO_FORMAT_U24_LE));
  assert_false(audio_format_is_signed(TSIG_AUDIO_FORMAT_U24_BE));
  assert_false(audio_format_is_signed(TSIG_AUDIO_FORMAT_U32));
  assert_false(audio_format_is_signed(TSIG_AUDIO_FORMAT_U32_LE));
  assert_false(audio_format_is_signed(TSIG_AUDIO_FORMAT_U32_BE));
  assert_false(audio_format_is_signed(TSIG_AUDIO_FORMAT_FLOAT));
  assert_false(audio_format_is_signed(TSIG_AUDIO_FORMAT_FLOAT_LE));
  assert_false(audio_format_is_signed(TSIG_AUDIO_FORMAT_FLOAT_BE));
  assert_false(audio_format_is_signed(TSIG_AUDIO_FORMAT_FLOAT64));
  assert_false(audio_format_is_signed(TSIG_AUDIO_FORMAT_FLOAT64_LE));
  assert_false(audio_format_is_signed(TSIG_AUDIO_FORMAT_FLOAT64_BE));
}

static void test_audio_format_is_le(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  bool b = tsig_audio_is_cpu_le();

  assert_false(audio_format_is_le(TSIG_AUDIO_FORMAT_UNKNOWN));
  assert_int_equal(audio_format_is_le(TSIG_AUDIO_FORMAT_S16), b);
  assert_true(audio_format_is_le(TSIG_AUDIO_FORMAT_S16_LE));
  assert_false(audio_format_is_le(TSIG_AUDIO_FORMAT_S16_BE));
  assert_int_equal(audio_format_is_le(TSIG_AUDIO_FORMAT_S24), b);
  assert_true(audio_format_is_le(TSIG_AUDIO_FORMAT_S24_LE));
  assert_false(audio_format_is_le(TSIG_AUDIO_FORMAT_S24_BE));
  assert_int_equal(audio_format_is_le(TSIG_AUDIO_FORMAT_S32), b);
  assert_true(audio_format_is_le(TSIG_AUDIO_FORMAT_S32_LE));
  assert_false(audio_format_is_le(TSIG_AUDIO_FORMAT_S32_BE));
  assert_int_equal(audio_format_is_le(TSIG_AUDIO_FORMAT_U16), b);
  assert_true(audio_format_is_le(TSIG_AUDIO_FORMAT_U16_LE));
  assert_false(audio_format_is_le(TSIG_AUDIO_FORMAT_U16_BE));
  assert_int_equal(audio_format_is_le(TSIG_AUDIO_FORMAT_U24), b);
  assert_true(audio_format_is_le(TSIG_AUDIO_FORMAT_U24_LE));
  assert_false(audio_format_is_le(TSIG_AUDIO_FORMAT_U24_BE));
  assert_int_equal(audio_format_is_le(TSIG_AUDIO_FORMAT_U32), b);
  assert_true(audio_format_is_le(TSIG_AUDIO_FORMAT_U32_LE));
  assert_false(audio_format_is_le(TSIG_AUDIO_FORMAT_U32_BE));
  assert_int_equal(audio_format_is_le(TSIG_AUDIO_FORMAT_FLOAT), b);
  assert_true(audio_format_is_le(TSIG_AUDIO_FORMAT_FLOAT_LE));
  assert_false(audio_format_is_le(TSIG_AUDIO_FORMAT_FLOAT_BE));
  assert_int_equal(audio_format_is_le(TSIG_AUDIO_FORMAT_FLOAT64), b);
  assert_true(audio_format_is_le(TSIG_AUDIO_FORMAT_FLOAT64_LE));
  assert_false(audio_format_is_le(TSIG_AUDIO_FORMAT_FLOAT64_BE));
}

static void test_audio_format_width(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_UNKNOWN), 0);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_S16), 2);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_S16_LE), 2);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_S16_BE), 2);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_S24), 3);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_S24_LE), 3);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_S24_BE), 3);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_S32), 4);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_S32_LE), 4);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_S32_BE), 4);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_U16), 2);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_U16_LE), 2);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_U16_BE), 2);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_U24), 3);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_U24_LE), 3);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_U24_BE), 3);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_U32), 4);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_U32_LE), 4);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_U32_BE), 4);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_FLOAT), 4);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_FLOAT_LE), 4);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_FLOAT_BE), 4);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_FLOAT64), 8);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_FLOAT64_LE), 8);
  assert_int_equal(audio_format_width(TSIG_AUDIO_FORMAT_FLOAT64_BE), 8);
}

static void test_tsig_audio_format(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  /* clang-format off */
  assert_int_equal(tsig_audio_format(""), TSIG_AUDIO_FORMAT_UNKNOWN);
  assert_int_equal(tsig_audio_format(NULL), TSIG_AUDIO_FORMAT_UNKNOWN);
  assert_int_equal(tsig_audio_format("asdf"), TSIG_AUDIO_FORMAT_UNKNOWN);
  assert_int_equal(tsig_audio_format("S16"), TSIG_AUDIO_FORMAT_S16);
  assert_int_equal(tsig_audio_format("S16"), TSIG_AUDIO_FORMAT_S16);
  assert_int_equal(tsig_audio_format("S16_LE"), TSIG_AUDIO_FORMAT_S16_LE);
  assert_int_equal(tsig_audio_format("S16_BE"), TSIG_AUDIO_FORMAT_S16_BE);
  assert_int_equal(tsig_audio_format("S24"), TSIG_AUDIO_FORMAT_S24);
  assert_int_equal(tsig_audio_format("S24_LE"), TSIG_AUDIO_FORMAT_S24_LE);
  assert_int_equal(tsig_audio_format("S24_BE"), TSIG_AUDIO_FORMAT_S24_BE);
  assert_int_equal(tsig_audio_format("S32"), TSIG_AUDIO_FORMAT_S32);
  assert_int_equal(tsig_audio_format("S32_LE"), TSIG_AUDIO_FORMAT_S32_LE);
  assert_int_equal(tsig_audio_format("S32_BE"), TSIG_AUDIO_FORMAT_S32_BE);
  assert_int_equal(tsig_audio_format("U16"), TSIG_AUDIO_FORMAT_U16);
  assert_int_equal(tsig_audio_format("U16_LE"), TSIG_AUDIO_FORMAT_U16_LE);
  assert_int_equal(tsig_audio_format("U16_BE"), TSIG_AUDIO_FORMAT_U16_BE);
  assert_int_equal(tsig_audio_format("U24"), TSIG_AUDIO_FORMAT_U24);
  assert_int_equal(tsig_audio_format("U24_LE"), TSIG_AUDIO_FORMAT_U24_LE);
  assert_int_equal(tsig_audio_format("U24_BE"), TSIG_AUDIO_FORMAT_U24_BE);
  assert_int_equal(tsig_audio_format("U32"), TSIG_AUDIO_FORMAT_U32);
  assert_int_equal(tsig_audio_format("U32_LE"), TSIG_AUDIO_FORMAT_U32_LE);
  assert_int_equal(tsig_audio_format("U32_BE"), TSIG_AUDIO_FORMAT_U32_BE);
  assert_int_equal(tsig_audio_format("FLOAT"), TSIG_AUDIO_FORMAT_FLOAT);
  assert_int_equal(tsig_audio_format("FLOAT_LE"), TSIG_AUDIO_FORMAT_FLOAT_LE);
  assert_int_equal(tsig_audio_format("FLOAT_BE"), TSIG_AUDIO_FORMAT_FLOAT_BE);
  assert_int_equal(tsig_audio_format("FLOAT64"), TSIG_AUDIO_FORMAT_FLOAT64);
  assert_int_equal(tsig_audio_format("FLOAT64_LE"), TSIG_AUDIO_FORMAT_FLOAT64_LE);
  assert_int_equal(tsig_audio_format("FLOAT64_BE"), TSIG_AUDIO_FORMAT_FLOAT64_BE);
  /* clang-format on */
}

static void test_tsig_audio_format_name(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  /* clang-format off */
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_S16), "S16");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_S16_LE), "S16_LE");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_S16_BE), "S16_BE");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_S24), "S24");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_S24_LE), "S24_LE");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_S24_BE), "S24_BE");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_S32), "S32");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_S32_LE), "S32_LE");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_S32_BE), "S32_BE");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_U16), "U16");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_U16_LE), "U16_LE");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_U16_BE), "U16_BE");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_U24), "U24");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_U24_LE), "U24_LE");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_U24_BE), "U24_BE");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_U32), "U32");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_U32_LE), "U32_LE");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_U32_BE), "U32_BE");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_FLOAT), "FLOAT");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_FLOAT_LE), "FLOAT_LE");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_FLOAT_BE), "FLOAT_BE");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_FLOAT64), "FLOAT64");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_FLOAT64_LE), "FLOAT64_LE");
  assert_string_equal(tsig_audio_format_name(TSIG_AUDIO_FORMAT_FLOAT64_BE), "FLOAT64_BE");
  /* clang-format on */
}

static void test_tsig_audio_format_phys_width(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  /* clang-format off */
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_UNKNOWN), 0);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_S16), 2);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_S16_LE), 2);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_S16_BE), 2);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_S24), 4);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_S24_LE), 4);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_S24_BE), 4);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_S32), 4);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_S32_LE), 4);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_S32_BE), 4);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_U16), 2);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_U16_LE), 2);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_U16_BE), 2);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_U24), 4);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_U24_LE), 4);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_U24_BE), 4);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_U32), 4);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_U32_LE), 4);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_U32_BE), 4);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_FLOAT), 4);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_FLOAT_LE), 4);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_FLOAT_BE), 4);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_FLOAT64), 8);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_FLOAT64_LE), 8);
  assert_int_equal(tsig_audio_format_phys_width(TSIG_AUDIO_FORMAT_FLOAT64_BE), 8);
  /* clang-format on */
}

static void test_tsig_audio_rate(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  assert_int_equal(tsig_audio_rate(""), TSIG_AUDIO_RATE_UNKNOWN);
  assert_int_equal(tsig_audio_rate(NULL), TSIG_AUDIO_RATE_UNKNOWN);
  assert_int_equal(tsig_audio_rate("asdf"), TSIG_AUDIO_RATE_UNKNOWN);
  assert_int_equal(tsig_audio_rate("22050"), TSIG_AUDIO_RATE_UNKNOWN);
  assert_int_equal(tsig_audio_rate("44100"), TSIG_AUDIO_RATE_44100);
  assert_int_equal(tsig_audio_rate("48000"), TSIG_AUDIO_RATE_48000);
  assert_int_equal(tsig_audio_rate("88200"), TSIG_AUDIO_RATE_88200);
  assert_int_equal(tsig_audio_rate("96000"), TSIG_AUDIO_RATE_96000);
  assert_int_equal(tsig_audio_rate("176400"), TSIG_AUDIO_RATE_176400);
  assert_int_equal(tsig_audio_rate("192000"), TSIG_AUDIO_RATE_192000);
  assert_int_equal(tsig_audio_rate("352800"), TSIG_AUDIO_RATE_352800);
  assert_int_equal(tsig_audio_rate("384000"), TSIG_AUDIO_RATE_384000);
}

static void test_tsig_audio_fill_buffer(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  double cb_buf[] = {-0.40869600005658424, 0.6852241982123343};
  uint8_t buf[128] = {0};

  tsig_audio_fill_buffer(TSIG_AUDIO_FORMAT_UNKNOWN, 1, 1, buf, cb_buf);
  uint8_t ref_unknown[128] = {0};
  assert_memory_equal(buf, ref_unknown, 128);

  tsig_audio_fill_buffer(TSIG_AUDIO_FORMAT_S16_LE, 1, 1, buf, cb_buf);
  uint8_t ref_s16_le[] = {0xaf, 0xcb};
  assert_memory_equal(buf, ref_s16_le, 2);
  tsig_audio_fill_buffer(TSIG_AUDIO_FORMAT_S16_BE, 1, 1, buf, cb_buf);
  uint8_t ref_s16_be[] = {0xcb, 0xaf};
  assert_memory_equal(buf, ref_s16_be, 2);
  tsig_audio_fill_buffer(TSIG_AUDIO_FORMAT_S24_LE, 1, 1, buf, cb_buf);
  uint8_t ref_s24_le[] = {0x00, 0xaf, 0xcb, 0xff};
  assert_memory_equal(buf, ref_s24_le, 3);
  tsig_audio_fill_buffer(TSIG_AUDIO_FORMAT_S24_BE, 1, 1, buf, cb_buf);
  uint8_t ref_s24_be[] = {0xff, 0xcb, 0xaf, 0x00};
  assert_memory_equal(&buf[1], &ref_s24_be[1], 3);
  tsig_audio_fill_buffer(TSIG_AUDIO_FORMAT_S32_LE, 1, 1, buf, cb_buf);
  uint8_t ref_s32_le[] = {0x00, 0x00, 0xaf, 0xcb};
  assert_memory_equal(buf, ref_s32_le, 4);
  tsig_audio_fill_buffer(TSIG_AUDIO_FORMAT_S32_BE, 1, 1, buf, cb_buf);
  uint8_t ref_s32_be[] = {0xcb, 0xaf, 0x00, 0x00};
  assert_memory_equal(buf, ref_s32_be, 4);
  tsig_audio_fill_buffer(TSIG_AUDIO_FORMAT_U16_LE, 1, 1, buf, cb_buf);
  uint8_t ref_u16_le[] = {0xaf, 0x4b};
  assert_memory_equal(buf, ref_u16_le, 2);
  tsig_audio_fill_buffer(TSIG_AUDIO_FORMAT_U16_BE, 1, 1, buf, cb_buf);
  uint8_t ref_u16_be[] = {0x4b, 0xaf};
  assert_memory_equal(buf, ref_u16_be, 2);
  tsig_audio_fill_buffer(TSIG_AUDIO_FORMAT_U24_LE, 1, 1, buf, cb_buf);
  uint8_t ref_u24_le[] = {0x00, 0xaf, 0x4b, 0x00};
  assert_memory_equal(buf, ref_u24_le, 3);
  tsig_audio_fill_buffer(TSIG_AUDIO_FORMAT_U24_BE, 1, 1, buf, cb_buf);
  uint8_t ref_u24_be[] = {0x00, 0x4b, 0xaf, 0x00};
  assert_memory_equal(&buf[1], &ref_u24_be[1], 3);
  tsig_audio_fill_buffer(TSIG_AUDIO_FORMAT_U32_LE, 1, 1, buf, cb_buf);
  uint8_t ref_u32_le[] = {0x00, 0x00, 0xaf, 0x4b};
  assert_memory_equal(buf, ref_u32_le, 4);
  tsig_audio_fill_buffer(TSIG_AUDIO_FORMAT_U32_BE, 1, 1, buf, cb_buf);
  uint8_t ref_u32_be[] = {0x4b, 0xaf, 0x00, 0x00};
  assert_memory_equal(buf, ref_u32_be, 4);
  tsig_audio_fill_buffer(TSIG_AUDIO_FORMAT_FLOAT_LE, 1, 1, buf, cb_buf);
  uint8_t ref_float_le[] = {0x00, 0x40, 0xd1, 0xbe};
  assert_memory_equal(buf, ref_float_le, 4);
  tsig_audio_fill_buffer(TSIG_AUDIO_FORMAT_FLOAT_BE, 1, 1, buf, cb_buf);
  uint8_t ref_float_be[] = {0xbe, 0xd1, 0x40, 0x00};
  assert_memory_equal(buf, ref_float_be, 4);
  tsig_audio_fill_buffer(TSIG_AUDIO_FORMAT_FLOAT64_LE, 1, 1, buf, cb_buf);
  uint8_t ref_float64_le[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0xda, 0xbf};
  assert_memory_equal(buf, ref_float64_le, 8);
  tsig_audio_fill_buffer(TSIG_AUDIO_FORMAT_FLOAT64_BE, 1, 1, buf, cb_buf);
  uint8_t ref_float64_be[] = {0xbf, 0xda, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00};
  assert_memory_equal(buf, ref_float64_be, 8);

  /* Multiple interleaved frames. */
  tsig_audio_fill_buffer(TSIG_AUDIO_FORMAT_S16_LE, 2, 2, buf, cb_buf);
  uint8_t ref_interleaved[] = {0xaf, 0xcb, 0xaf, 0xcb, 0xb4, 0x57, 0xb4, 0x57};
  assert_memory_equal(buf, ref_interleaved, 8);
}

static void test_tsig_is_cpu_le(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  volatile uint32_t n = 0x01234567;
  uint8_t *p = (uint8_t *)&n;

  assert_true(tsig_audio_is_cpu_le() == (*p == 0x67));
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_audio_format_is_float),
      cmocka_unit_test(test_audio_format_is_signed),
      cmocka_unit_test(test_audio_format_is_le),
      cmocka_unit_test(test_audio_format_width),
      cmocka_unit_test(test_tsig_audio_format),
      cmocka_unit_test(test_tsig_audio_format_name),
      cmocka_unit_test(test_tsig_audio_format_phys_width),
      cmocka_unit_test(test_tsig_audio_rate),
      cmocka_unit_test(test_tsig_audio_fill_buffer),
      cmocka_unit_test(test_tsig_is_cpu_le),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
