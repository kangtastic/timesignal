// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * test_cfg.c: Test program configuration.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "cfg.c"

#include "mock_log.c"

#include "audio.c"
#include "backend.c"
#include "datetime.c"
#include "iir.c"
#include "mapping.c"
#include "station.c"
#include "util.c"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

static void test_cfg_parse_offset(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  long msecs;

  assert_false(cfg_parse_offset("", &msecs));
  assert_false(cfg_parse_offset(" ", &msecs));
  assert_false(cfg_parse_offset(" .", &msecs));
  assert_false(cfg_parse_offset(".. ", &msecs));
  assert_false(cfg_parse_offset(" asdf ", &msecs));

  assert_true(cfg_parse_offset("0.", &msecs));
  assert_int_equal(msecs, 0);
  assert_true(cfg_parse_offset("0.0", &msecs));
  assert_int_equal(msecs, 0);
  assert_true(cfg_parse_offset("0.00", &msecs));
  assert_int_equal(msecs, 0);
  assert_true(cfg_parse_offset("0.000", &msecs));
  assert_int_equal(msecs, 0);
  assert_true(cfg_parse_offset("0.0000", &msecs));
  assert_int_equal(msecs, 0);
  assert_true(cfg_parse_offset("0.1234", &msecs));
  assert_int_equal(msecs, 123);
  assert_true(cfg_parse_offset("0.9999", &msecs));
  assert_int_equal(msecs, 999);
  assert_false(cfg_parse_offset(".1234", &msecs));
  assert_false(cfg_parse_offset("0.1@3", &msecs));
  assert_false(cfg_parse_offset("0.123!", &msecs));
  assert_false(cfg_parse_offset("!0.123", &msecs));

  assert_true(cfg_parse_offset("0", &msecs));
  assert_int_equal(msecs, 0);
  assert_true(cfg_parse_offset("1", &msecs));
  assert_int_equal(msecs, 1000);
  assert_true(cfg_parse_offset("12.345", &msecs));
  assert_int_equal(msecs, 12345);
  assert_true(cfg_parse_offset("59.999", &msecs));
  assert_int_equal(msecs, 59999);
  assert_false(cfg_parse_offset("60", &msecs));
  assert_false(cfg_parse_offset("61.123", &msecs));
  assert_false(cfg_parse_offset("s9.999", &msecs));
  assert_false(cfg_parse_offset("s9.9g9", &msecs));

  assert_true(cfg_parse_offset("0:0", &msecs));
  assert_int_equal(msecs, 0);
  assert_true(cfg_parse_offset("00:00", &msecs));
  assert_int_equal(msecs, 0);
  assert_true(cfg_parse_offset("59:59.999", &msecs));
  assert_int_equal(msecs, 3599999);
  assert_false(cfg_parse_offset("60:00.000", &msecs));
  assert_false(cfg_parse_offset("s9:59.999", &msecs));
  assert_false(cfg_parse_offset("59:s9.999", &msecs));
  assert_false(cfg_parse_offset("59:59.9g9", &msecs));
  assert_false(cfg_parse_offset("s9:s9.9g9", &msecs));

  assert_true(cfg_parse_offset("0:0:0", &msecs));
  assert_int_equal(msecs, 0);
  assert_true(cfg_parse_offset("00:00:00.000", &msecs));
  assert_int_equal(msecs, 0);
  assert_true(cfg_parse_offset("23:59:59.999", &msecs));
  assert_int_equal(msecs, 86399999);
  assert_false(cfg_parse_offset("24:00:00.000", &msecs));
  assert_false(cfg_parse_offset("z3:59:59.999", &msecs));
  assert_false(cfg_parse_offset("23:s9:59.999", &msecs));
  assert_false(cfg_parse_offset("23:59:s9.999", &msecs));
  assert_false(cfg_parse_offset("23:59:59.9g9", &msecs));
  assert_false(cfg_parse_offset("z3:s9:s9.9g9", &msecs));

  assert_true(cfg_parse_offset("+23:59:59.999 ", &msecs));
  assert_int_equal(msecs, 86399999);
  assert_true(cfg_parse_offset(" -23:59:59.999", &msecs));
  assert_int_equal(msecs, -86399999);
}

static void test_cfg_parse_base(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  int64_t msecs;

  assert_false(cfg_parse_base("2099", &msecs));
  assert_false(cfg_parse_base("20991", &msecs));
  assert_false(cfg_parse_base("2099-0", &msecs));
  assert_false(cfg_parse_base("2099-13", &msecs));
  assert_false(cfg_parse_base("2099-120", &msecs));
  assert_false(cfg_parse_base("2099-12-0", &msecs));
  assert_false(cfg_parse_base("2099-12-32", &msecs));
  assert_false(cfg_parse_base("2099-12-31", &msecs));
  assert_false(cfg_parse_base("2099-12-310", &msecs));
  assert_false(cfg_parse_base("2099-12-31 -1", &msecs));
  assert_false(cfg_parse_base("2099-12-31 24", &msecs));
  assert_false(cfg_parse_base("2099-12-31 23:-1", &msecs));
  assert_false(cfg_parse_base("2099-12-31 23:60", &msecs));
  assert_false(cfg_parse_base("2099-12-31 23:59:-1", &msecs));
  assert_false(cfg_parse_base("2099-12-31 23:59:60", &msecs));
  assert_false(cfg_parse_base("2099-12-31 23:59:590", &msecs));
  assert_false(cfg_parse_base("2099-12-31 23:59:590000", &msecs));
  assert_false(cfg_parse_base("2099-12-31 23:59:59 0000", &msecs));
  assert_false(cfg_parse_base("2099-12-31 23:59:59+2400", &msecs));
  assert_false(cfg_parse_base("2099-12-31 23:59:59+2360", &msecs));
  assert_false(cfg_parse_base("2099-12-31 23:59:59-2400", &msecs));
  assert_false(cfg_parse_base("2099-12-31 23:59:59-2360", &msecs));
  assert_false(cfg_parse_base("2099-12-31 23:59:59+359", &msecs));
  assert_false(cfg_parse_base("2099-12-31 23:59:59-235", &msecs));
  assert_false(cfg_parse_base("2099-12-31 23:59:59+25", &msecs));

  assert_false(cfg_parse_base("1970-1-1 0:0:0+0001", &msecs));
  assert_true(cfg_parse_base("1970-1-1 0:0:0-0001", &msecs));
  assert_int_equal(msecs, 60000);
  assert_true(cfg_parse_base("1970-1-1 0:0:0", &msecs));
  assert_int_equal(msecs, 0);
  assert_true(cfg_parse_base("1970-1-1 0:0", &msecs));
  assert_int_equal(msecs, 0);

  assert_true(cfg_parse_base(" 2099-12-31 23:59:59+0000", &msecs));
  assert_int_equal(msecs, 4102444799000);
  assert_true(cfg_parse_base("2099-12-31 23:59:59-0000 ", &msecs));
  assert_int_equal(msecs, 4102444799000);
  assert_true(cfg_parse_base(" 2099-12-31 23:59:59-2359 ", &msecs));
  assert_int_equal(msecs, 4102531139000);
}

static void test_cfg_strtol(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  long n;
  assert_false(cfg_strtol("", &n));
  assert_false(cfg_strtol("0x3", &n));
  assert_false(cfg_strtol(" ", &n));
  assert_false(cfg_strtol("!", &n));
  assert_false(cfg_strtol("12345 z", &n));
  assert_false(cfg_strtol("12345 12345", &n));
  assert_false(cfg_strtol("111111111111111111111", &n));
  assert_true(cfg_strtol(" 12345 ", &n));
  assert_int_equal(n, 12345);
  assert_true(cfg_strtol("-12345", &n));
  assert_int_equal(n, -12345);
  assert_true(cfg_strtol("+0", &n));
  assert_int_equal(n, 0);
  assert_true(cfg_strtol("-0", &n));
  assert_int_equal(n, 0);
}

static void test_cfg_set_station(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  tsig_cfg_t cfg;
  tsig_log_t log;

  assert_true(cfg_set_station(&cfg, &log, "BPC"));
  assert_int_equal(cfg.station, TSIG_STATION_ID_BPC);
  assert_true(cfg_set_station(&cfg, &log, "BpC"));
  assert_int_equal(cfg.station, TSIG_STATION_ID_BPC);
  assert_true(cfg_set_station(&cfg, &log, "DCF77"));
  assert_int_equal(cfg.station, TSIG_STATION_ID_DCF77);
  assert_true(cfg_set_station(&cfg, &log, "DcF77"));
  assert_int_equal(cfg.station, TSIG_STATION_ID_DCF77);
  assert_true(cfg_set_station(&cfg, &log, "JJY"));
  assert_int_equal(cfg.station, TSIG_STATION_ID_JJY);
  assert_true(cfg_set_station(&cfg, &log, "JjY"));
  assert_int_equal(cfg.station, TSIG_STATION_ID_JJY);
  assert_true(cfg_set_station(&cfg, &log, "JJY40"));
  assert_int_equal(cfg.station, TSIG_STATION_ID_JJY);
  assert_true(cfg_set_station(&cfg, &log, "JjY40"));
  assert_int_equal(cfg.station, TSIG_STATION_ID_JJY);
  assert_true(cfg_set_station(&cfg, &log, "JJY60"));
  assert_int_equal(cfg.station, TSIG_STATION_ID_JJY60);
  assert_true(cfg_set_station(&cfg, &log, "JjY60"));
  assert_int_equal(cfg.station, TSIG_STATION_ID_JJY60);
  assert_true(cfg_set_station(&cfg, &log, "MSF"));
  assert_int_equal(cfg.station, TSIG_STATION_ID_MSF);
  assert_true(cfg_set_station(&cfg, &log, "MsF"));
  assert_int_equal(cfg.station, TSIG_STATION_ID_MSF);
  assert_true(cfg_set_station(&cfg, &log, "WWVB"));
  assert_int_equal(cfg.station, TSIG_STATION_ID_WWVB);
  assert_true(cfg_set_station(&cfg, &log, "WwVb"));
  assert_int_equal(cfg.station, TSIG_STATION_ID_WWVB);

  cfg.station = TSIG_STATION_ID_JJY60;
  assert_false(cfg_set_station(&cfg, &log, ""));
  assert_int_equal(cfg.station, TSIG_STATION_ID_JJY60);
  assert_false(cfg_set_station(&cfg, &log, "WWVC"));
  assert_int_equal(cfg.station, TSIG_STATION_ID_JJY60);
}

static void test_cfg_set_base(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  tsig_cfg_t cfg;
  tsig_log_t log;

  assert_true(cfg_set_base(&cfg, &log, "2099-12-31 23:59:59-2359"));
  assert_int_equal(cfg.base, 4102531139000);
  assert_true(cfg_set_base(&cfg, &log, "1970-1-1 00:00:00+0000"));
  assert_int_equal(cfg.base, 0);

  cfg.base = 12345;
  assert_false(cfg_set_base(&cfg, &log, "1970-1-1 0:0:0+0001"));
  assert_int_equal(cfg.base, 12345);
  assert_false(cfg_set_base(&cfg, &log, "invalid"));
  assert_int_equal(cfg.base, 12345);
  assert_false(cfg_set_base(&cfg, &log, ""));
  assert_int_equal(cfg.base, 12345);
}

static void test_cfg_set_offset(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  tsig_cfg_t cfg;
  tsig_log_t log;

  assert_true(cfg_set_offset(&cfg, &log, "-23:59:59.999"));
  assert_int_equal(cfg.offset, -86399999);
  assert_true(cfg_set_offset(&cfg, &log, "-0:0:0"));
  assert_int_equal(cfg.offset, 0);
  assert_true(cfg_set_offset(&cfg, &log, "-0:0"));
  assert_int_equal(cfg.offset, 0);
  assert_true(cfg_set_offset(&cfg, &log, "-0"));
  assert_int_equal(cfg.offset, 0);
  assert_true(cfg_set_offset(&cfg, &log, "0"));
  assert_int_equal(cfg.offset, 0);
  assert_true(cfg_set_offset(&cfg, &log, "+0"));
  assert_int_equal(cfg.offset, 0);
  assert_true(cfg_set_offset(&cfg, &log, "+0:0"));
  assert_int_equal(cfg.offset, 0);
  assert_true(cfg_set_offset(&cfg, &log, "+0:0:0"));
  assert_int_equal(cfg.offset, 0);
  assert_true(cfg_set_offset(&cfg, &log, "23:59:59.999"));
  assert_int_equal(cfg.offset, 86399999);
  assert_true(cfg_set_offset(&cfg, &log, "+23:59:59.999"));
  assert_int_equal(cfg.offset, 86399999);

  cfg.offset = 12345;
  assert_false(cfg_set_base(&cfg, &log, "-24:00:00"));
  assert_int_equal(cfg.offset, 12345);
  assert_false(cfg_set_base(&cfg, &log, "24:00:00"));
  assert_int_equal(cfg.offset, 12345);
  assert_false(cfg_set_base(&cfg, &log, "+24:00:00"));
  assert_int_equal(cfg.offset, 12345);
  assert_false(cfg_set_offset(&cfg, &log, "invalid"));
  assert_int_equal(cfg.offset, 12345);
  assert_false(cfg_set_offset(&cfg, &log, ""));
  assert_int_equal(cfg.offset, 12345);
}

static void test_cfg_set_dut1(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  tsig_cfg_t cfg;
  tsig_log_t log;

  assert_true(cfg_set_dut1(&cfg, &log, "-999"));
  assert_int_equal(cfg.dut1, -999);
  assert_true(cfg_set_dut1(&cfg, &log, "-0"));
  assert_int_equal(cfg.dut1, 0);
  assert_true(cfg_set_dut1(&cfg, &log, "0"));
  assert_int_equal(cfg.dut1, 0);
  assert_true(cfg_set_dut1(&cfg, &log, "+0"));
  assert_int_equal(cfg.dut1, 0);
  assert_true(cfg_set_dut1(&cfg, &log, "999"));
  assert_int_equal(cfg.dut1, 999);
  assert_true(cfg_set_dut1(&cfg, &log, "+999"));
  assert_int_equal(cfg.dut1, 999);

  cfg.dut1 = 12345;
  assert_false(cfg_set_dut1(&cfg, &log, "-1000"));
  assert_int_equal(cfg.dut1, 12345);
  assert_false(cfg_set_dut1(&cfg, &log, "1000"));
  assert_int_equal(cfg.dut1, 12345);
  assert_false(cfg_set_dut1(&cfg, &log, "+1000"));
  assert_int_equal(cfg.dut1, 12345);
  assert_false(cfg_set_dut1(&cfg, &log, "invalid"));
  assert_int_equal(cfg.dut1, 12345);
  assert_false(cfg_set_dut1(&cfg, &log, ""));
  assert_int_equal(cfg.dut1, 12345);
}

static void test_cfg_set_timeout(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  tsig_cfg_t cfg;
  tsig_log_t log;

  assert_true(cfg_set_timeout(&cfg, &log, "1"));
  assert_int_equal(cfg.timeout, 1);
  assert_true(cfg_set_timeout(&cfg, &log, "02"));
  assert_int_equal(cfg.timeout, 2);
  assert_true(cfg_set_timeout(&cfg, &log, "0:03"));
  assert_int_equal(cfg.timeout, 3);
  assert_true(cfg_set_timeout(&cfg, &log, "+0:04"));
  assert_int_equal(cfg.timeout, 4);
  assert_true(cfg_set_timeout(&cfg, &log, "23:59:58"));
  assert_int_equal(cfg.timeout, 86398);
  assert_true(cfg_set_timeout(&cfg, &log, "23:59:59.999"));
  assert_int_equal(cfg.timeout, 86399);

  cfg.timeout = 12345;
  assert_false(cfg_set_timeout(&cfg, &log, "0"));
  assert_int_equal(cfg.timeout, 12345);
  assert_false(cfg_set_timeout(&cfg, &log, "-1"));
  assert_int_equal(cfg.timeout, 12345);
  assert_false(cfg_set_timeout(&cfg, &log, "60"));
  assert_int_equal(cfg.timeout, 12345);
  assert_false(cfg_set_timeout(&cfg, &log, "23:59:60"));
  assert_int_equal(cfg.timeout, 12345);
  assert_false(cfg_set_timeout(&cfg, &log, "23:60:59"));
  assert_int_equal(cfg.timeout, 12345);
  assert_false(cfg_set_timeout(&cfg, &log, "24:00:00"));
  assert_int_equal(cfg.timeout, 12345);
  assert_false(cfg_set_timeout(&cfg, &log, "invalid"));
  assert_int_equal(cfg.timeout, 12345);
  assert_false(cfg_set_timeout(&cfg, &log, ""));
  assert_int_equal(cfg.timeout, 12345);
}

static void test_cfg_set_backend(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  tsig_cfg_t cfg;
  tsig_log_t log;

  assert_true(cfg_set_backend(&cfg, &log, "PipeWire"));
  assert_int_equal(cfg.backend, TSIG_BACKEND_PIPEWIRE);
  assert_true(cfg_set_backend(&cfg, &log, "PiPeWiRe"));
  assert_int_equal(cfg.backend, TSIG_BACKEND_PIPEWIRE);
  assert_true(cfg_set_backend(&cfg, &log, "pw"));
  assert_int_equal(cfg.backend, TSIG_BACKEND_PIPEWIRE);
  assert_true(cfg_set_backend(&cfg, &log, "Pw"));
  assert_int_equal(cfg.backend, TSIG_BACKEND_PIPEWIRE);
  assert_true(cfg_set_backend(&cfg, &log, "PulseAudio"));
  assert_int_equal(cfg.backend, TSIG_BACKEND_PULSE);
  assert_true(cfg_set_backend(&cfg, &log, "PuLsEaUdIo"));
  assert_int_equal(cfg.backend, TSIG_BACKEND_PULSE);
  assert_true(cfg_set_backend(&cfg, &log, "Pulse"));
  assert_int_equal(cfg.backend, TSIG_BACKEND_PULSE);
  assert_true(cfg_set_backend(&cfg, &log, "PuLsE"));
  assert_int_equal(cfg.backend, TSIG_BACKEND_PULSE);
  assert_true(cfg_set_backend(&cfg, &log, "pa"));
  assert_int_equal(cfg.backend, TSIG_BACKEND_PULSE);
  assert_true(cfg_set_backend(&cfg, &log, "Pa"));
  assert_int_equal(cfg.backend, TSIG_BACKEND_PULSE);
  assert_true(cfg_set_backend(&cfg, &log, "ALSA"));
  assert_int_equal(cfg.backend, TSIG_BACKEND_ALSA);
  assert_true(cfg_set_backend(&cfg, &log, "AlSa"));
  assert_int_equal(cfg.backend, TSIG_BACKEND_ALSA);

  cfg.backend = TSIG_BACKEND_PIPEWIRE;
  assert_false(cfg_set_backend(&cfg, &log, "WirePipe"));
  assert_int_equal(cfg.backend, TSIG_BACKEND_PIPEWIRE);
  assert_false(cfg_set_backend(&cfg, &log, ""));
  assert_int_equal(cfg.backend, TSIG_BACKEND_PIPEWIRE);
}

static void test_cfg_set_device(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  tsig_cfg_t cfg;
  tsig_log_t log;
  char str[TSIG_CFG_DEVICE_SIZE + 1];

  for (uint32_t i = 0; i < sizeof(str) - 2; i++)
    str[i] = 'a';
  str[sizeof(str) - 2] = 'b';
  str[sizeof(str) - 1] = '\0';

  assert_true(cfg_set_device(&cfg, &log, str));
  str[sizeof(str) - 2] = '\0';
  assert_string_equal(cfg.device, str);

  assert_true(cfg_set_device(&cfg, &log, "any string"));
  assert_string_equal(cfg.device, "any string");
  assert_true(cfg_set_device(&cfg, &log, ""));
  assert_string_equal(cfg.device, "");
}

static void test_cfg_set_format(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  tsig_cfg_t cfg;
  tsig_log_t log;

  assert_true(cfg_set_format(&cfg, &log, "S16"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_S16);
  assert_true(cfg_set_format(&cfg, &log, "s16_le"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_S16_LE);
  assert_true(cfg_set_format(&cfg, &log, "S16_BE"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_S16_BE);
  assert_true(cfg_set_format(&cfg, &log, "s24"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_S24);
  assert_true(cfg_set_format(&cfg, &log, "S24_LE"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_S24_LE);
  assert_true(cfg_set_format(&cfg, &log, "s24_be"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_S24_BE);
  assert_true(cfg_set_format(&cfg, &log, "S32"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_S32);
  assert_true(cfg_set_format(&cfg, &log, "s32_le"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_S32_LE);
  assert_true(cfg_set_format(&cfg, &log, "S32_BE"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_S32_BE);
  assert_true(cfg_set_format(&cfg, &log, "U16"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_U16);
  assert_true(cfg_set_format(&cfg, &log, "u16_le"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_U16_LE);
  assert_true(cfg_set_format(&cfg, &log, "U16_BE"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_U16_BE);
  assert_true(cfg_set_format(&cfg, &log, "u24"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_U24);
  assert_true(cfg_set_format(&cfg, &log, "U24_LE"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_U24_LE);
  assert_true(cfg_set_format(&cfg, &log, "u24_be"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_U24_BE);
  assert_true(cfg_set_format(&cfg, &log, "U32"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_U32);
  assert_true(cfg_set_format(&cfg, &log, "u32_le"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_U32_LE);
  assert_true(cfg_set_format(&cfg, &log, "U32_BE"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_U32_BE);
  assert_true(cfg_set_format(&cfg, &log, "FLOAT"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_FLOAT);
  assert_true(cfg_set_format(&cfg, &log, "float_le"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_FLOAT_LE);
  assert_true(cfg_set_format(&cfg, &log, "FLOAT_BE"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_FLOAT_BE);
  assert_true(cfg_set_format(&cfg, &log, "FLOAT64"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_FLOAT64);
  assert_true(cfg_set_format(&cfg, &log, "float64_le"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_FLOAT64_LE);
  assert_true(cfg_set_format(&cfg, &log, "FLOAT64_BE"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_FLOAT64_BE);

  cfg.format = TSIG_AUDIO_FORMAT_S16;
  assert_false(cfg_set_format(&cfg, &log, "s64"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_S16);
  assert_false(cfg_set_format(&cfg, &log, "invalid"));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_S16);
  assert_false(cfg_set_format(&cfg, &log, ""));
  assert_int_equal(cfg.format, TSIG_AUDIO_FORMAT_S16);
}

static void test_cfg_set_rate(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  tsig_cfg_t cfg;
  tsig_log_t log;

  assert_true(cfg_set_rate(&cfg, &log, "44100"));
  assert_int_equal(cfg.rate, TSIG_AUDIO_RATE_44100);
  assert_true(cfg_set_rate(&cfg, &log, "48000"));
  assert_int_equal(cfg.rate, TSIG_AUDIO_RATE_48000);
  assert_true(cfg_set_rate(&cfg, &log, "88200"));
  assert_int_equal(cfg.rate, TSIG_AUDIO_RATE_88200);
  assert_true(cfg_set_rate(&cfg, &log, "96000"));
  assert_int_equal(cfg.rate, TSIG_AUDIO_RATE_96000);
  assert_true(cfg_set_rate(&cfg, &log, "176400"));
  assert_int_equal(cfg.rate, TSIG_AUDIO_RATE_176400);
  assert_true(cfg_set_rate(&cfg, &log, "192000"));
  assert_int_equal(cfg.rate, TSIG_AUDIO_RATE_192000);
  assert_true(cfg_set_rate(&cfg, &log, "352800"));
  assert_int_equal(cfg.rate, TSIG_AUDIO_RATE_352800);
  assert_true(cfg_set_rate(&cfg, &log, "384000"));
  assert_int_equal(cfg.rate, TSIG_AUDIO_RATE_384000);

  cfg.rate = TSIG_AUDIO_RATE_44100;
  assert_false(cfg_set_rate(&cfg, &log, "22050"));
  assert_int_equal(cfg.rate, TSIG_AUDIO_RATE_44100);
  assert_false(cfg_set_rate(&cfg, &log, "44101"));
  assert_int_equal(cfg.rate, TSIG_AUDIO_RATE_44100);
  assert_false(cfg_set_rate(&cfg, &log, "invalid"));
  assert_int_equal(cfg.rate, TSIG_AUDIO_RATE_44100);
  assert_false(cfg_set_rate(&cfg, &log, ""));
  assert_int_equal(cfg.rate, TSIG_AUDIO_RATE_44100);
}

static void test_cfg_set_channels(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  tsig_cfg_t cfg;
  tsig_log_t log;

  assert_true(cfg_set_channels(&cfg, &log, "1"));
  assert_int_equal(cfg.channels, 1);
  assert_true(cfg_set_channels(&cfg, &log, "1023"));
  assert_int_equal(cfg.channels, 1023);
  assert_true(cfg_set_channels(&cfg, &log, "+1023"));
  assert_int_equal(cfg.channels, 1023);

  cfg.channels = 123;
  assert_false(cfg_set_channels(&cfg, &log, "0"));
  assert_int_equal(cfg.channels, 123);
  assert_false(cfg_set_channels(&cfg, &log, "-1"));
  assert_int_equal(cfg.channels, 123);
  assert_false(cfg_set_channels(&cfg, &log, "1024"));
  assert_int_equal(cfg.channels, 123);
  assert_false(cfg_set_channels(&cfg, &log, "invalid"));
  assert_int_equal(cfg.channels, 123);
  assert_false(cfg_set_channels(&cfg, &log, ""));
  assert_int_equal(cfg.channels, 123);
}

static void test_cfg_set_smooth(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  tsig_cfg_t cfg;
  tsig_log_t log;

  cfg.smooth = false;
  assert_true(cfg_set_smooth(&cfg, &log, NULL));
  assert_true(cfg.smooth);
  cfg.smooth = false;
  assert_true(cfg_set_smooth(&cfg, &log, "on"));
  assert_true(cfg.smooth);
  cfg.smooth = true;
  assert_true(cfg_set_smooth(&cfg, &log, "OfF"));
  assert_false(cfg.smooth);

  cfg.smooth = true;
  assert_false(cfg_set_smooth(&cfg, &log, "invalid"));
  assert_true(cfg.smooth);
  cfg.smooth = true;
  assert_false(cfg_set_smooth(&cfg, &log, ""));
  assert_true(cfg.smooth);
}

static void test_cfg_set_ultrasound(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  tsig_cfg_t cfg;
  tsig_log_t log;

  cfg.ultrasound = false;
  assert_true(cfg_set_ultrasound(&cfg, &log, NULL));
  assert_true(cfg.ultrasound);
  cfg.ultrasound = false;
  assert_true(cfg_set_ultrasound(&cfg, &log, "on"));
  assert_true(cfg.ultrasound);
  cfg.ultrasound = true;
  assert_true(cfg_set_ultrasound(&cfg, &log, "OfF"));
  assert_false(cfg.ultrasound);

  cfg.ultrasound = true;
  assert_false(cfg_set_ultrasound(&cfg, &log, "invalid"));
  assert_true(cfg.ultrasound);
  cfg.ultrasound = true;
  assert_false(cfg_set_ultrasound(&cfg, &log, ""));
  assert_true(cfg.ultrasound);
}

static void test_cfg_set_audible(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  tsig_cfg_t cfg;
  tsig_log_t log;

  cfg.audible = false;
  assert_true(cfg_set_audible(&cfg, &log, NULL));
  assert_true(cfg.audible);
  cfg.audible = false;
  assert_true(cfg_set_audible(&cfg, &log, "on"));
  assert_true(cfg.audible);
  cfg.audible = true;
  assert_true(cfg_set_audible(&cfg, &log, "OfF"));
  assert_false(cfg.audible);

  cfg.audible = true;
  assert_false(cfg_set_audible(&cfg, &log, "invalid"));
  assert_true(cfg.audible);
  cfg.audible = true;
  assert_false(cfg_set_audible(&cfg, &log, ""));
  assert_true(cfg.audible);
}

static void test_cfg_set_log_file(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  tsig_cfg_t cfg;
  tsig_log_t log;
  char str[TSIG_CFG_PATH_SIZE + 1];

  for (uint32_t i = 0; i < sizeof(str) - 2; i++)
    str[i] = 'a';
  str[sizeof(str) - 2] = 'b';
  str[sizeof(str) - 1] = '\0';

  assert_true(cfg_set_log_file(&cfg, &log, str));
  str[sizeof(str) - 2] = '\0';
  assert_string_equal(cfg.log_file, str);

  assert_true(cfg_set_log_file(&cfg, &log, "any string"));
  assert_string_equal(cfg.log_file, "any string");
  assert_true(cfg_set_log_file(&cfg, &log, ""));
  assert_string_equal(cfg.log_file, "");
}

static void test_cfg_set_syslog(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  tsig_cfg_t cfg;
  tsig_log_t log;

  cfg.syslog = false;
  assert_true(cfg_set_syslog(&cfg, &log, NULL));
  assert_true(cfg.syslog);
  cfg.syslog = false;
  assert_true(cfg_set_syslog(&cfg, &log, "on"));
  assert_true(cfg.syslog);
  cfg.syslog = true;
  assert_true(cfg_set_syslog(&cfg, &log, "OfF"));
  assert_false(cfg.syslog);

  cfg.syslog = true;
  assert_false(cfg_set_syslog(&cfg, &log, "invalid"));
  assert_true(cfg.syslog);
  cfg.syslog = true;
  assert_false(cfg_set_syslog(&cfg, &log, ""));
  assert_true(cfg.syslog);
}

static void test_cfg_set_verbose(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  tsig_cfg_t cfg;
  tsig_log_t log;

  cfg.verbose = false;
  assert_true(cfg_set_verbose(&cfg, &log, NULL));
  assert_true(cfg.verbose);
  cfg.verbose = false;
  assert_true(cfg_set_verbose(&cfg, &log, "on"));
  assert_true(cfg.verbose);
  cfg.verbose = true;
  assert_true(cfg_set_verbose(&cfg, &log, "OfF"));
  assert_false(cfg.verbose);

  cfg.verbose = true;
  assert_false(cfg_set_verbose(&cfg, &log, "invalid"));
  assert_true(cfg.verbose);
  cfg.verbose = true;
  assert_false(cfg_set_verbose(&cfg, &log, ""));
  assert_true(cfg.verbose);
}

static void test_cfg_set_quiet(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  tsig_cfg_t cfg;
  tsig_log_t log;

  cfg.quiet = false;
  assert_true(cfg_set_quiet(&cfg, &log, NULL));
  assert_true(cfg.quiet);
  cfg.quiet = false;
  assert_true(cfg_set_quiet(&cfg, &log, "on"));
  assert_true(cfg.quiet);
  cfg.quiet = true;
  assert_true(cfg_set_quiet(&cfg, &log, "OfF"));
  assert_false(cfg.quiet);

  cfg.quiet = true;
  assert_false(cfg_set_quiet(&cfg, &log, "invalid"));
  assert_true(cfg.quiet);
  cfg.quiet = true;
  assert_false(cfg_set_quiet(&cfg, &log, ""));
  assert_true(cfg.quiet);
}

static void test_cfg_process_file_line(void **state) {
  (void)state; /* Suppress unused parameter warning. */

  char line[128];
  char *value;
  char *name;

  strcpy(line, "");
  cfg_process_file_line(line, &name, &value);
  assert_null(name);
  assert_null(value);
  strcpy(line, "# foo=bar");
  cfg_process_file_line(line, &name, &value);
  assert_null(name);
  assert_null(value);
  strcpy(line, " foo ");
  cfg_process_file_line(line, &name, &value);
  assert_string_equal(name, "foo");
  assert_null(value);
  strcpy(line, " fo#o ");
  cfg_process_file_line(line, &name, &value);
  assert_string_equal(name, "fo");
  assert_null(value);
  strcpy(line, " foo=bar ");
  cfg_process_file_line(line, &name, &value);
  assert_string_equal(name, "foo");
  assert_string_equal(value, "bar");
  strcpy(line, " foo = bar  baz ");
  cfg_process_file_line(line, &name, &value);
  assert_string_equal(name, "foo");
  assert_string_equal(value, "bar  baz");
  strcpy(line, " foo = #bar ");
  cfg_process_file_line(line, &name, &value);
  assert_string_equal(name, "foo");
  assert_string_equal(value, "");
  strcpy(line, " foo = ' bar baz ' ");
  cfg_process_file_line(line, &name, &value);
  assert_string_equal(name, "foo");
  assert_string_equal(value, " bar baz ");
  strcpy(line, " foo = \" bar baz \" ");
  cfg_process_file_line(line, &name, &value);
  assert_string_equal(name, "foo");
  assert_string_equal(value, " bar baz ");
  strcpy(line, " foo = \" bar#baz \" ");
  cfg_process_file_line(line, &name, &value);
  assert_string_equal(name, "foo");
  assert_string_equal(value, " bar#baz ");
  strcpy(line, " foo = \" bar#baz ' ");
  cfg_process_file_line(line, &name, &value);
  assert_string_equal(name, "foo");
  assert_string_equal(value, "\" bar");
  strcpy(line, " foo = \" bar #baz ' ");
  cfg_process_file_line(line, &name, &value);
  assert_string_equal(name, "foo");
  assert_string_equal(value, "\" bar");
  strcpy(line, " foo = \" bar baz ' ");
  cfg_process_file_line(line, &name, &value);
  assert_string_equal(name, "foo");
  assert_string_equal(value, "\" bar baz '");
  strcpy(line, " foo = ' ");
  cfg_process_file_line(line, &name, &value);
  assert_string_equal(name, "foo");
  assert_string_equal(value, "'");
  strcpy(line, " foo ==");
  cfg_process_file_line(line, &name, &value);
  assert_string_equal(name, "foo");
  assert_string_equal(value, "=");
  strcpy(line, " foo ! ' ");
  cfg_process_file_line(line, &name, &value);
  assert_string_equal(name, "foo");
  assert_null(value);
  strcpy(line, " foo1 2 ! ' ");
  cfg_process_file_line(line, &name, &value);
  assert_string_equal(name, "foo1");
  assert_null(value);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_cfg_parse_offset),
      cmocka_unit_test(test_cfg_parse_base),
      cmocka_unit_test(test_cfg_strtol),
      cmocka_unit_test(test_cfg_set_station),
      cmocka_unit_test(test_cfg_set_base),
      cmocka_unit_test(test_cfg_set_offset),
      cmocka_unit_test(test_cfg_set_dut1),
      cmocka_unit_test(test_cfg_set_timeout),
      cmocka_unit_test(test_cfg_set_backend),
      cmocka_unit_test(test_cfg_set_device),
      cmocka_unit_test(test_cfg_set_format),
      cmocka_unit_test(test_cfg_set_rate),
      cmocka_unit_test(test_cfg_set_channels),
      cmocka_unit_test(test_cfg_set_smooth),
      cmocka_unit_test(test_cfg_set_ultrasound),
      cmocka_unit_test(test_cfg_set_audible),
      cmocka_unit_test(test_cfg_set_log_file),
      cmocka_unit_test(test_cfg_set_syslog),
      cmocka_unit_test(test_cfg_set_verbose),
      cmocka_unit_test(test_cfg_set_quiet),
      cmocka_unit_test(test_cfg_process_file_line),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
