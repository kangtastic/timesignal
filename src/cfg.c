// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * cfg.c: Program configuration.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "cfg.h"

#include "audio.h"
#include "backend.h"
#include "datetime.h"
#include "defaults.h"
#include "log.h"
#include "station.h"
#include "util.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>

#include <getopt.h>

/** Configurable audio backends. */
#ifdef TSIG_HAVE_BACKENDS
#if defined(TSIG_HAVE_PIPEWIRE) && defined(TSIG_HAVE_PULSE) && \
    defined(TSIG_HAVE_ALSA)
#define TSIG_CFG_BACKENDS "pipewire, pulse, alsa"
#elif defined(TSIG_HAVE_PIPEWIRE) && defined(TSIG_HAVE_PULSE)
#define TSIG_CFG_BACKENDS "pipewire, pulse"
#elif defined(TSIG_HAVE_PIPEWIRE) && defined(TSIG_HAVE_ALSA)
#define TSIG_CFG_BACKENDS "pipewire, alsa"
#else
#define TSIG_CFG_BACKENDS "pulse, alsa"
#endif /* TSIG_HAVE_PIPEWIRE, TSIG_HAVE_PULSE, TSIG_HAVE_ALSA */
#endif /* TSIG_HAVE_BACKENDS */

/** Pointer to a setter function. */
typedef bool (*cfg_setter_t)(tsig_cfg_t *cfg, tsig_log_t *log, const char *str);

/** Setter function info. */
typedef struct cfg_setter_info {
  const char *name;
  cfg_setter_t setter;
} cfg_setter_info_t;

/** Forward declarations for function prototypes. */
static bool cfg_set_station(tsig_cfg_t *cfg, tsig_log_t *log, const char *str);
static bool cfg_set_base(tsig_cfg_t *cfg, tsig_log_t *log, const char *str);
static bool cfg_set_offset(tsig_cfg_t *cfg, tsig_log_t *log, const char *str);
static bool cfg_set_dut1(tsig_cfg_t *cfg, tsig_log_t *log, const char *str);
static bool cfg_set_timeout(tsig_cfg_t *cfg, tsig_log_t *log, const char *str);

#ifdef TSIG_HAVE_BACKENDS
static bool cfg_set_backend(tsig_cfg_t *cfg, tsig_log_t *log, const char *str);
#endif /* TSIG_HAVE_BACKENDS */

#ifdef TSIG_HAVE_ALSA
static bool cfg_set_device(tsig_cfg_t *cfg, tsig_log_t *log, const char *str);
#endif /* TSIG_HAVE_ALSA */

static bool cfg_set_format(tsig_cfg_t *cfg, tsig_log_t *log, const char *str);
static bool cfg_set_rate(tsig_cfg_t *cfg, tsig_log_t *log, const char *str);
static bool cfg_set_channels(tsig_cfg_t *cfg, tsig_log_t *log, const char *str);
static bool cfg_set_smooth(tsig_cfg_t *cfg, tsig_log_t *log, const char *str);
static bool cfg_set_ultrasound(tsig_cfg_t *cfg, tsig_log_t *log,
                               const char *str);
static bool cfg_set_log_file(tsig_cfg_t *cfg, tsig_log_t *log, const char *str);
static bool cfg_set_syslog(tsig_cfg_t *cfg, tsig_log_t *log, const char *str);
static bool cfg_set_verbosity(tsig_cfg_t *cfg, tsig_log_t *log,
                              const char *str);

#ifdef TSIG_DEBUG
static void cfg_print(tsig_cfg_t *cfg, tsig_log_t *log);
#endif /* TSIG_DEBUG */

/** DUT1 limits (exclusive). */
static const long cfg_dut1_min = -1000;
static const long cfg_dut1_max = 1000;

/** User timeout limits (exclusive). */
static const long cfg_timeout_min = 999;
static const long cfg_timeout_max = 86400000;

/** Channel count limits (exclusive). */
static const long cfg_channels_min = 0;
static const long cfg_channels_max = 1024;

/** Time conversions. */
static const long cfg_msecs_hour = 3600000;
static const long cfg_msecs_min = 60000;
static const long cfg_msecs_sec = 1000;

/** Help string. */
static const char cfg_help_fmt[] = {
    /* clang-format off */
    TSIG_DEFAULTS_NAME " " TSIG_DEFAULTS_VERSION " <" TSIG_DEFAULTS_URL ">\n"
    TSIG_DEFAULTS_DESCRIPTION "\n"
    "\n"
    "Usage: " TSIG_DEFAULTS_NAME " [OPTION]...\n"
    "\n"
    "Time signal options:\n"
    "  -s, --station=STATION    time station to emulate\n"
    "  -b, --base=BASE          time base in YYYY-MM-DD HH:mm:ss[(+-)hhmm] format\n"
    "  -o, --offset=OFFSET      user offset in [+-]HH:mm:ss[.SSS] format\n"
    "  -d, --dut1=DUT1          DUT1 value in ms (only for MSF and WWVB)\n"
    "\n"
    "Timeout options:\n"
    "  -t, --timeout=TIMEOUT    time to run before exiting in HH:mm:ss format\n"
    "\n"
    "Sound options (rarely needed):\n"

#ifdef TSIG_HAVE_BACKENDS
    "  -m, --method=METHOD      output method\n"
    #endif /* TSIG_HAVE_BACKENDS */

#ifdef TSIG_HAVE_ALSA
    "  -D, --device=DEVICE      output device (only for ALSA)\n"
#endif /* TSIG_HAVE_ALSA */

    "  -f, --format=FORMAT      output sample format\n"
    "  -r, --rate=RATE          output sample rate\n"
    "  -c, --channels=CHANNELS  output channels\n"
    "  -S, --smooth             smooth rapid gain changes in output waveform\n"
    "  -u, --ultrasound         enable ultrasound output (MAY DAMAGE EQUIPMENT)\n"
    "\n"
    "Configuration file options:\n"
    "  -C, --config=CONFIG_FILE load options from a file\n"
    "\n"
    "Logging options:\n"
    "  -l, --log=LOG_FILE       log messages to a file\n"
    "  -L, --syslog             log messages to syslog\n"
    "\n"
    "Miscellaneous:\n"
    "  -h, --help               show this help and exit\n"
    "  -v, --verbose            increase verbosity level\n"
    "\n"
    "Recognized option values (not all work on all systems):\n"
    "  time station   BPC, DCF77, JJY, JJY60, MSF, WWVB\n"
    "  time base      1970-01-01 00:00:00+0000 to 9999-12-31 23:59:59+2359\n"
    "  user offset    -23:59:59.999 to 23:59:59.999\n"
    "  DUT1 value     -999 to 999\n"
    "  timeout        00:00:01 to 23:59:59\n"

#ifdef TSIG_HAVE_BACKENDS
    "  output method  " TSIG_CFG_BACKENDS "\n"
#endif /* TSIG_HAVE_BACKENDS */

#ifdef TSIG_HAVE_ALSA
    "  output device  ALSA device name\n"
#endif /* TSIG_HAVE_ALSA */

    "  sample format  S16, S16_LE, S16_BE, U16, U16_LE, U16_BE,\n"
    "                 S24, S24_LE, S24_BE, U24, U24_LE, U24_BE,\n"
    "                 S32, S32_LE, S32_BE, U32, U32_LE, U32_BE,\n"
    "                 FLOAT, FLOAT_LE, FLOAT_BE,\n"
    "                 FLOAT64, FLOAT64_LE, FLOAT64_BE\n"
    "  sample rate    44100, 48000, 88200, 96000,\n"
    "                 176400, 192000, 352800, 384000\n"
    "  channels       1-1023\n"
    "  smooth gain    provide to turn on\n"
    "  ultrasound     provide to turn on (MAY DAMAGE EQUIPMENT)\n"
    "  config file    filesystem path\n"
    "  log file       filesystem path\n"
    "  syslog         provide to turn on\n"
    "  verbosity      provide to increase (maximum twice)\n"
    "\n"
    "Default option values:\n"
    "  time station   WWVB\n"
    "  time base      current system time\n"
    "  user offset    00:00:00.000\n"
    "  DUT1 value     0\n"
    "  timeout        forever\n"

#ifdef TSIG_HAVE_BACKENDS
    "  output method  autodetect\n"
#endif /* TSIG_HAVE_BACKENDS */

#ifdef TSIG_HAVE_ALSA
    "  ALSA device    default\n"
#endif /* TSIG_HAVE_ALSA */

    "  sample format  S16\n"
    "  sample rate    48000\n"
    "  channels       1\n"
    "  smooth gain    off\n"
    "  ultrasound     off\n"
    "  config file    none\n"
    "  log file       none\n"
    "  syslog         off\n"
    "  verbosity      0\n"
    "\n"
    /* clang-format on */
};

/** Default program configuration. */
static tsig_cfg_t cfg_default = {
    .station = TSIG_STATION_ID_WWVB,
    .base = TSIG_STATION_BASE_SYSTEM,
    .offset = 0,
    .dut1 = 0,
    .timeout = 0,

#ifdef TSIG_HAVE_BACKENDS
    .backend = TSIG_BACKEND_UNKNOWN,
#endif /* TSIG_HAVE_BACKENDS */

#ifdef TSIG_HAVE_ALSA
    .device = {"default"},
#endif /* TSIG_HAVE_ALSA */

    .format = TSIG_AUDIO_FORMAT_S16,
    .rate = TSIG_AUDIO_RATE_48000,
    .channels = 1,
    .smooth = false,
    .ultrasound = false,
    .log_file = {""},
    .syslog = false,
    .verbosity = 0,
};

/** Long options. */
static struct option cfg_longopts[] = {
    {"station", required_argument, NULL, 's'},
    {"base", required_argument, NULL, 'b'},
    {"offset", required_argument, NULL, 'o'},
    {"dut1", required_argument, NULL, 'd'},
    {"timeout", required_argument, NULL, 't'},

#ifdef TSIG_HAVE_BACKENDS
    {"method", required_argument, NULL, 'm'},
#endif /* TSIG_HAVE_BACKENDS */

#ifdef TSIG_HAVE_ALSA
    {"device", required_argument, NULL, 'D'},
#endif /* TSIG_HAVE_ALSA */

    {"format", required_argument, NULL, 'f'},
    {"rate", required_argument, NULL, 'r'},
    {"channels", required_argument, NULL, 'c'},
    {"smooth", no_argument, NULL, 'S'},
    {"ultrasound", no_argument, NULL, 'u'},
    {"config", required_argument, NULL, 'C'},
    {"log", required_argument, NULL, 'l'},
    {"syslog", no_argument, NULL, 'L'},
    {"help", no_argument, NULL, 'h'},
    {"verbose", no_argument, NULL, 'v'},
    {NULL, 0, NULL, 0},
};

/** Short options. */
static const char cfg_opts[] = {
    "s:b:o:d:t:"

#ifdef TSIG_HAVE_BACKENDS
    "m:"
#endif /* TSIG_HAVE_BACKENDS */

#ifdef TSIG_HAVE_ALSA
    "D:"
#endif /* TSIG_HAVE_ALSA */

    "f:r:c:SuC:l:Lhv",
};

/** Setter functions for a configuration file. */
static cfg_setter_info_t cfg_setter_info[] = {
    {"station", &cfg_set_station},
    {"base", &cfg_set_base},
    {"offset", &cfg_set_offset},
    {"dut1", &cfg_set_dut1},
    {"timeout", &cfg_set_timeout},

#ifdef TSIG_HAVE_BACKENDS
    {"method", &cfg_set_backend},
#endif /* TSIG_HAVE_BACKENDS */

#ifdef TSIG_HAVE_ALSA
    {"device", &cfg_set_device},
#endif /* TSIG_HAVE_ALSA */

    {"format", &cfg_set_format},
    {"rate", &cfg_set_rate},
    {"channels", &cfg_set_channels},
    {"smooth", &cfg_set_smooth},
    {"ultrasound", &cfg_set_ultrasound},
    {"log", &cfg_set_log_file},
    {"syslog", &cfg_set_syslog},
    {"verbose", &cfg_set_verbosity},
    {NULL, NULL},
};

/** Parse a string in [+-][[H]H:][[m]m:][s]s[.[S[S[S]]]] format. */
static bool cfg_parse_offset(const char *str, long *out_msecs) {
  const char *l = NULL;
  const char *p = NULL;
  const char *r = NULL;
  const char *s = str;
  long sign = 1;
  long hour = 0;
  long min = 0;
  long sec = 0;
  long msec = 0;

  /* Find the trimmed bounds of the string and the floating point. */
  for (; *s; s++) {
    if (!isspace(*s)) {
      if (!l)
        l = s;
      r = s + 1;
    }
    if (*s == '.') {
      if (!p)
        p = s;
      else
        return false;
    }
  }

  /* Ensure we have something to parse. */
  if (!l)
    return false;

  /* Parse up to three digits from the right of the floating point. */
  if (p) {
    /* Floating point must be preceded or followed by a digit. */
    if (!((l < p && isdigit(p[-1])) || (p + 1 < r && isdigit(p[1]))))
      return false;
    s = p + 1;
    if (s < r && isdigit(*s))
      msec += 100 * (*s++ - '0');
    if (s < r && isdigit(*s))
      msec += 10 * (*s++ - '0');
    if (s < r && isdigit(*s))
      msec += *s++ - '0';
    /* Consume remaining digits. */
    for (; s < r; s++)
      if (!isdigit(*s))
        return false;
    r = p;
  }

  s = r - 1;

  /* Parse seconds. */
  if (!(l <= s && isdigit(*s)))
    return false;
  sec += *s-- - '0';
  if (l <= s && isdigit(*s))
    sec += 10 * (*s-- - '0');
  if (!(0 <= sec && sec <= 59))
    return false;

  /* Parse minutes if they exist. */
  if (l <= s && *s == ':') {
    if (!(l < s && isdigit(*--s)))
      return false;
    min += *s-- - '0';
    if (l <= s && isdigit(*s))
      min += 10 * (*s-- - '0');
    if (!(0 <= min && min <= 59))
      return false;
  }

  /* Parse hours if they exist. */
  if (l <= s && *s == ':') {
    if (!(l < s && isdigit(*--s)))
      return false;
    hour += *s-- - '0';
    if (l <= s && isdigit(*s))
      hour += 10 * (*s-- - '0');
    if (!(0 <= hour && hour <= 23))
      return false;
  }

  /* Parse the sign if it exists. */
  if (l <= s && (*s == '+' || *s == '-'))
    if (*s-- == '-')
      sign = -1;

  /* Ensure we have nothing more to parse. */
  if (l != s + 1)
    return false;

  *out_msecs = cfg_msecs_hour * hour;
  *out_msecs += cfg_msecs_min * min;
  *out_msecs += cfg_msecs_sec * sec;
  *out_msecs += msec;
  *out_msecs *= sign;

  return true;
}

/** Parse a string in YYYY-[M]M-[D]D [H]H:[m]m[:[s]s][(+-)hhmm] format. */
static bool cfg_parse_base(const char *str, int64_t *out_msecs) {
  const char *l = NULL;
  const char *r = NULL;
  const char *s = str;
  bool tz_neg = false;
  int tz_hour = 0;
  int tz_min = 0;
  int sec = 0;
  int year;
  int mon;
  int day;
  int hour;
  int min;
  int tz;

  /* Find the trimmed bounds of the string. */
  for (; *s; s++) {
    if (!isspace(*s)) {
      if (!l)
        l = s;
      r = s + 1;
    }
  }

  /* Ensure we have something to parse. */
  if (!l)
    return false;

  s = l;

  /* Parse the date. */
  if (!(isdigit(s[0]) && isdigit(s[1]) && isdigit(s[2]) && isdigit(s[3])))
    return false;
  year = 1000 * (*s++ - '0');
  year += 100 * (*s++ - '0');
  year += 10 * (*s++ - '0');
  year += *s++ - '0';
  if (!(1970 <= year && year <= 9999 && *s++ == '-'))
    return false;
  if (!isdigit(*s))
    return false;
  mon = *s++ - '0';
  if (isdigit(*s))
    mon = 10 * mon + (*s++ - '0');
  if (!(1 <= mon && mon <= 12 && *s++ == '-'))
    return false;
  if (!isdigit(*s))
    return false;
  day = *s++ - '0';
  if (isdigit(*s))
    day = 10 * day + (*s++ - '0');
  if (!(1 <= day && day <= tsig_datetime_days_in_mon(year, mon) && *s++ == ' '))
    return false;

  /* Parse hours and minutes. */
  if (!isdigit(*s))
    return false;
  hour = *s++ - '0';
  if (isdigit(*s))
    hour = 10 * hour + (*s++ - '0');
  if (!(0 <= hour && hour <= 23 && *s++ == ':'))
    return false;
  if (!isdigit(*s))
    return false;
  min = *s++ - '0';
  if (isdigit(*s))
    min = 10 * min + (*s++ - '0');
  if (!(0 <= min && min <= 59))
    return false;

  /* Parse seconds if they exist. */
  if (*s == ':') {
    if (!isdigit(*++s))
      return false;
    sec = *s++ - '0';
    if (isdigit(*s))
      sec = 10 * sec + (*s++ - '0');
    if (!(0 <= sec && sec <= 59))
      return false;
  }

  /* Parse the timezone if it exists. */
  if (*s == '+' || *s == '-') {
    tz_neg = *s++ == '-';
    if (!(isdigit(s[0]) && isdigit(s[1]) && isdigit(s[2]) && isdigit(s[3])))
      return false;
    tz_hour = 10 * (*s++ - '0');
    tz_hour += *s++ - '0';
    if (!(0 <= tz_hour && tz_hour <= 23))
      return false;
    tz_min = 10 * (*s++ - '0');
    tz_min += *s++ - '0';
    if (!(0 <= tz_min && tz_min <= 59))
      return false;
  }

  /* Ensure we have nothing more to parse. */
  if (s != r)
    return false;

  /* Calculate the timezone offset (in minutes). */
  tz = 60 * tz_hour + tz_min;
  if (tz_neg)
    tz = -tz;

  /* Ensure the date and time we parsed isn't before the epoch. */
  if (year == 1970 && mon == 1 && day == 1 &&
      3600 * hour + 60 * min + sec - 60 * tz < 0)
    return false;

  *out_msecs =
      tsig_datetime_make_timestamp(year, mon, day, hour, min, sec, 0, tz);

  return true;
}

/** Parse a string to a long with error detection. */
static bool cfg_strtol(const char *str, long *out_n) {
  char *endptr;
  long n;

  errno = 0;

  while (isspace(*str))
    str++;

  n = strtol(str, &endptr, 10);

  if (errno || str == endptr)
    return false;

  for (; *endptr; endptr++)
    if (!isspace(*endptr))
      return false;

  *out_n = n;

  return true;
}

/** Setter for station. */
static bool cfg_set_station(tsig_cfg_t *cfg, tsig_log_t *log, const char *str) {
  tsig_station_id_t station = tsig_station_id(str);

  if (station == TSIG_STATION_ID_UNKNOWN) {
    tsig_log_err("invalid station \"%s\"", str);
    return false;
  }

  cfg->station = station;
  return true;
}

/** Setter for base. */
static bool cfg_set_base(tsig_cfg_t *cfg, tsig_log_t *log, const char *str) {
  int64_t base;

  if (!cfg_parse_base(str, &base)) {
    tsig_log_err(
        "invalid base time \"%s\" must be between "
        "1970-01-01 00:00:00+0000 and 9999-12-31 23:59:59+2359",
        str);
    return false;
  }

  cfg->base = base;
  return true;
}

/** Setter for offset. */
static bool cfg_set_offset(tsig_cfg_t *cfg, tsig_log_t *log, const char *str) {
  long offset;

  if (!cfg_parse_offset(str, &offset)) {
    tsig_log_err(
        "invalid offset \"%s\" must be between -23:59:59.999 and 23:59:59.999",
        str);
    return false;
  }

  cfg->offset = (int32_t)offset;
  return true;
}

/** Setter for dut1. */
static bool cfg_set_dut1(tsig_cfg_t *cfg, tsig_log_t *log, const char *str) {
  long dut1;

  if (!cfg_strtol(str, &dut1)) {
    tsig_log_err("invalid dut1 \"%s\"", str);
    return false;
  }

  if (!(cfg_dut1_min < dut1 && dut1 < cfg_dut1_max)) {
    tsig_log_err("dut1 %ld must be between %ld and %ld", dut1, cfg_dut1_min + 1,
                 cfg_dut1_max - 1);
    return false;
  }

  cfg->dut1 = (int16_t)dut1;
  return true;
}

/** Setter for timeout. */
static bool cfg_set_timeout(tsig_cfg_t *cfg, tsig_log_t *log, const char *str) {
  long timeout;

  if (!cfg_parse_offset(str, &timeout) ||
      (!(cfg_timeout_min < timeout && timeout < cfg_timeout_max))) {
    tsig_log_err("invalid timeout \"%s\" must be between 00:00:01 and 23:59:59",
                 str);
    return false;
  }

  cfg->timeout = (unsigned)timeout / cfg_msecs_sec;
  return true;
}

#ifdef TSIG_HAVE_BACKENDS
/** Setter for backend. */
static bool cfg_set_backend(tsig_cfg_t *cfg, tsig_log_t *log, const char *str) {
  tsig_backend_t backend = tsig_backend(str);

  if (backend == TSIG_BACKEND_UNKNOWN) {
    tsig_log_err("invalid method \"%s\"", str);
    return false;
  }

  cfg->backend = backend;
  return true;
}
#endif /* TSIG_HAVE_BACKENDS */

#ifdef TSIG_HAVE_ALSA
/** Setter for device. */
static bool cfg_set_device(tsig_cfg_t *cfg, tsig_log_t *log, const char *str) {
  (void)log; /* Suppress unused parameter warning. */

  strncpy(cfg->device, str, sizeof(cfg->device));
  cfg->device[sizeof(cfg->device) - 1] = '\0';

  return true;
}
#endif /* TSIG_HAVE_ALSA */

/** Setter for format. */
static bool cfg_set_format(tsig_cfg_t *cfg, tsig_log_t *log, const char *str) {
  tsig_audio_format_t format = tsig_audio_format(str);

  if (format == TSIG_AUDIO_FORMAT_UNKNOWN) {
    tsig_log_err("invalid format \"%s\"", str);
    return false;
  }

  cfg->format = format;
  return true;
}

/** Setter for rate. */
static bool cfg_set_rate(tsig_cfg_t *cfg, tsig_log_t *log, const char *str) {
  tsig_audio_rate_t rate = tsig_audio_rate(str);

  if (rate == TSIG_AUDIO_RATE_UNKNOWN) {
    tsig_log_err("invalid rate \"%s\"", str);
    return false;
  }

  cfg->rate = (uint32_t)rate;
  return true;
}

/** Setter for channels. */
static bool cfg_set_channels(tsig_cfg_t *cfg, tsig_log_t *log,
                             const char *str) {
  long channels;

  if (!cfg_strtol(str, &channels)) {
    tsig_log_err("invalid channels \"%s\"", str);
    return false;
  }

  if (!(cfg_channels_min < channels && channels < cfg_channels_max)) {
    tsig_log_err("channels %ld must be between %ld and %ld", channels,
                 cfg_channels_min + 1, cfg_channels_max - 1);
    return false;
  }

  cfg->channels = (uint16_t)channels;
  return true;
}

/** Setter for smooth. */
static bool cfg_set_smooth(tsig_cfg_t *cfg, tsig_log_t *log, const char *str) {
  if (!str || !tsig_util_strcasecmp(str, "on")) {
    cfg->smooth = true;
  } else if (!tsig_util_strcasecmp(str, "off")) {
    cfg->smooth = false;
  } else {
    tsig_log_err("smooth \"%s\" must be \"on\" or \"off\"", str);
    return false;
  }

  return true;
}

/** Setter for ultrasound. */
static bool cfg_set_ultrasound(tsig_cfg_t *cfg, tsig_log_t *log,
                               const char *str) {
  if (!str || !tsig_util_strcasecmp(str, "on")) {
    cfg->ultrasound = true;
  } else if (!tsig_util_strcasecmp(str, "off")) {
    cfg->ultrasound = false;
  } else {
    tsig_log_err("ultrasound \"%s\" must be \"on\" or \"off\"", str);
    return false;
  }

  return true;
}

/** Setter for log_file. */
static bool cfg_set_log_file(tsig_cfg_t *cfg, tsig_log_t *log,
                             const char *str) {
  (void)log; /* Suppress unused parameter warning. */

  strncpy(cfg->log_file, str, sizeof(cfg->log_file));
  cfg->log_file[sizeof(cfg->log_file) - 1] = '\0';

  return true;
}

/** Setter for syslog. */
static bool cfg_set_syslog(tsig_cfg_t *cfg, tsig_log_t *log, const char *str) {
  if (!str || !tsig_util_strcasecmp(str, "on")) {
    cfg->syslog = true;
  } else if (!tsig_util_strcasecmp(str, "off")) {
    cfg->syslog = false;
  } else {
    tsig_log_err("syslog \"%s\" must be \"on\" or \"off\"", str);
    return false;
  }

  return true;
}

/** Setter for verbosity. */
static bool cfg_set_verbosity(tsig_cfg_t *cfg, tsig_log_t *log,
                              const char *str) {
  long verbosity;

  if (!cfg_strtol(str, &verbosity) || !(0 <= verbosity && verbosity <= 2)) {
    tsig_log_err("invalid verbosity \"%s\"", str);
    return false;
  }

  cfg->verbosity = verbosity;
  return true;
}

/** Find setter function for a configuration file option name. */
static int cfg_setter_index(char *name) {
  if (!name)
    return -1;

  for (int i = 0; cfg_setter_info[i].name || cfg_setter_info[i].setter; i++)
    if (!tsig_util_strcasecmp(name, cfg_setter_info[i].name))
      return i;

  return -1;
}

/** Extract option name and value from a configuration file line. */
static void cfg_process_file_line(char line[], char **out_name,
                                  char **out_value) {
  char *comment = NULL;
  char *equals = NULL;
  char *lquote = NULL;
  char *rquote = NULL;
  char *s = line;

  *out_value = NULL;
  *out_name = NULL;

  for (; *s; s++) {
    if (*s == '#') {
      /* Stop processing upon an unquoted comment marker '#'. */
      if (!lquote)
        break;

      /* We might still need the position of the first comment marker later. */
      if (!comment)
        comment = s;
    }

    /* Find the start position of the option name. */
    if (!*out_name) {
      if (isspace(*s))
        continue;
      if (!isalnum(*s))
        break;
      *out_name = s;
    }

    /* Find the equals sign. */
    else if (!equals) {
      if (isalnum(*s))
        continue;
      if (isspace(*s)) {
        *s = '\0';
        continue;
      }
      if (*s != '=')
        break;
      equals = s;
    }

    /* Find the start position of the value. */
    else if (!*out_value) {
      if (!lquote && isspace(*s))
        continue;
      if (!lquote && (*s == '\'' || *s == '"')) {
        lquote = s;
        continue;
      }
      *out_value = s;
    }

    /*
     * Quoted values are everything (even '#') between an opening
     * single/double quote mark and a corresponding closing quote mark.
     * Nested quote marks or alternating quote mark types are not handled.
     *
     * Unquoted values are everything (even single/double quotes) up to
     * EOL or '#' with the rightmost group of trailing spaces trimmed.
     */
    else if (lquote) {
      if (*s == *lquote) {
        rquote = s;
        break;
      }
    }
  }

  /* Terminate name if it was not followed by spaces before the equals sign. */
  if (equals)
    *equals = '\0';

  /*
   * Seemingly quoted values may actually be unquoted if the closing quote
   * mark is missing or of a different type than the opening quote mark.
   */
  if (lquote && !rquote) {
    *out_value = lquote;
    if (comment)
      s = comment;
  }

  /* Terminate at unquoted '#', unexpected non-'=', or closing quote mark. */
  *s = '\0';

  /* Trim trailing spaces in an unquoted value (not needed otherwise). */
  if (*out_value && !(lquote && rquote))
    while (*out_value < s && isspace(s[-1]))
      *(--s) = '\0';

  /* If we have an equals sign, value is at least "". */
  if (equals && !*out_value)
    *out_value = "";
}

/** Parse a configuration file. */
static bool cfg_parse_file(tsig_cfg_t *cfg, tsig_log_t *log, const char *path) {
  FILE *file = fopen(path, "rb");
  bool is_ok = true;
  char *line = NULL;
  char *value;
  char *name;
  size_t n;

  if (!file) {
    if (!strcmp(path, TSIG_DEFAULTS_CFG_FILE))
      return true; /* Don't try to be clever re: errno; it's not worth it. */

    tsig_log_err("failed to open config file \"%s\": %s", path,
                 strerror(errno));
    return false;
  }

  for (int line_num = 1; is_ok; line_num++) {
    if (getline(&line, &n, file) == -1) {
      free(line); /* getline() still allocated a buffer. */
      break;
    }

    cfg_process_file_line(line, &name, &value);

    if (!name && !value)
      goto continue_free_line;

    int k = cfg_setter_index(name);
    if (k < 0) {
      tsig_log_err("option \"%s\" on line %d of config file \"%s\" is invalid",
                   name, line_num, path);
      is_ok = false;
      goto continue_free_line;
    }

    const char *option_name = cfg_setter_info[k].name;
    cfg_setter_t setter = cfg_setter_info[k].setter;
    bool is_value_required = strcmp(name, "smooth") &&
                             strcmp(name, "ultrasound") &&
                             strcmp(name, "syslog");

    if (!value && is_value_required) {
      tsig_log_err(
          "option \"%s\" on line %d of config file \"%s\" requires a value",
          option_name, line_num, path);
      is_ok = false;
      goto continue_free_line;
    }

    if (!setter(cfg, log, value)) {
      tsig_log_err(
          "failed to set option \"%s\" on line %d of config file \"%s\"",
          option_name, line_num, path);
      is_ok = false;
    }

  continue_free_line:
    free(line);
    line = NULL; /* So getline() knows to allocate a new buffer. */
  }

  if (ferror(file)) {
    tsig_log_err("failed to read config file \"%s\": %s", strerror(errno));
    is_ok = false;
  }

  if (fclose(file) < 0) {
    tsig_log_err("failed to close config file \"%s\": %s", strerror(errno));
    is_ok = false;
  }

  return is_ok;
}

#ifdef TSIG_DEBUG
/** Print initialized program configuration. */
static void cfg_print(tsig_cfg_t *cfg, tsig_log_t *log) {
  const char *station = tsig_station_name(cfg->station);
  const char *format = tsig_audio_format_name(cfg->format);

#ifdef TSIG_HAVE_BACKENDS
  const char *backend = tsig_backend_name(cfg->backend);
#endif /* TSIG_HAVE_BACKENDS */

  tsig_log_dbg("tsig_cfg_t %p = {", cfg);
  tsig_log_dbg("  .station    = %s,", station);
  tsig_log_dbg("  .base       = %" PRIi64 ",", cfg->base);
  tsig_log_dbg("  .offset     = %" PRIi32 ",", cfg->offset);
  tsig_log_dbg("  .dut1       = %" PRIi16 ",", cfg->dut1);
  tsig_log_dbg("  .timeout    = %u,", cfg->timeout);

#ifdef TSIG_HAVE_BACKENDS
  tsig_log_dbg("  .backend    = %s,", backend);
#endif /* TSIG_HAVE_BACKENDS */

#ifdef TSIG_HAVE_ALSA
  tsig_log_dbg("  .device     = \"%s\",", cfg->device);
#endif /* TSIG_HAVE_ALSA */

  tsig_log_dbg("  .format     = %s,", format);
  tsig_log_dbg("  .rate       = %" PRIu32 ",", cfg->rate);
  tsig_log_dbg("  .channels   = %" PRIu16 ",", cfg->channels);
  tsig_log_dbg("  .smooth     = %d,", cfg->smooth);
  tsig_log_dbg("  .ultrasound = %d,", cfg->ultrasound);
  tsig_log_dbg("  .log_file   = \"%s\",", cfg->log_file);
  tsig_log_dbg("  .syslog     = %d,", cfg->syslog);
  tsig_log_dbg("  .verbosity  = %d,", cfg->verbosity);
  tsig_log_dbg("};");
}
#endif /* TSIG_DEBUG */

/**
 * Initialize program configuration.
 *
 * @param cfg Uninitialized program configuration.
 * @param log Initialized logging context. Will be initialized further.
 * @param argc Command-line argument count.
 * @param argv Command-line argument vector.
 * @return Return code indicating whether initialization was successful,
 *  and if so, whether the user printed the help message.
 */
tsig_cfg_init_result_t tsig_cfg_init(tsig_cfg_t *cfg, tsig_log_t *log, int argc,
                                     char *argv[]) {
  const char *cfg_file_path = TSIG_DEFAULTS_CFG_FILE;
  tsig_cfg_t cfg_file = cfg_default;
  bool is_ok = true;
  bool help = false;
  int opt;

  bool got_station = false;
  bool got_base = false;
  bool got_offset = false;
  bool got_dut1 = false;
  bool got_timeout = false;

#ifdef TSIG_HAVE_BACKENDS
  bool got_backend = false;
#endif /* TSIG_HAVE_BACKENDS */

#ifdef TSIG_HAVE_ALSA
  bool got_device = false;
#endif /* TSIG_HAVE_ALSA */

  bool got_format = false;
  bool got_rate = false;
  bool got_channels = false;
  bool got_smooth = false;
  bool got_ultrasound = false;
  bool got_log_file = false;
  bool got_syslog = false;
  bool got_verbosity = false;

  *cfg = cfg_default;

  while (is_ok) {
    opt = getopt_long(argc, argv, cfg_opts, cfg_longopts, NULL);
    if (opt < 0)
      break;

    /* NOTE: Logging while parsing cmdline args is to console only. */
    switch (opt) {
      case 's':
        is_ok = cfg_set_station(cfg, log, optarg);
        got_station = true;
        break;
      case 'b':
        is_ok = cfg_set_base(cfg, log, optarg);
        got_base = true;
        break;
      case 'o':
        is_ok = cfg_set_offset(cfg, log, optarg);
        got_offset = true;
        break;
      case 'd':
        is_ok = cfg_set_dut1(cfg, log, optarg);
        got_dut1 = true;
        break;
      case 't':
        is_ok = cfg_set_timeout(cfg, log, optarg);
        got_timeout = true;
        break;

#ifdef TSIG_HAVE_BACKENDS
      case 'm':
        is_ok = cfg_set_backend(cfg, log, optarg);
        got_backend = true;
        break;
#endif /* TSIG_HAVE_BACKENDS */

#ifdef TSIG_HAVE_ALSA
      case 'D':
        is_ok = cfg_set_device(cfg, log, optarg);
        got_device = true;
        break;
#endif /* TSIG_HAVE_ALSA */

      case 'f':
        is_ok = cfg_set_format(cfg, log, optarg);
        got_format = true;
        break;
      case 'r':
        is_ok = cfg_set_rate(cfg, log, optarg);
        got_rate = true;
        break;
      case 'c':
        is_ok = cfg_set_channels(cfg, log, optarg);
        got_channels = true;
        break;
      case 'S':
        cfg->smooth = true;
        got_smooth = true;
        break;
      case 'u':
        cfg->ultrasound = true;
        got_ultrasound = true;
        break;
      case 'C':
        cfg_file_path = optarg;
        break;
      case 'l':
        is_ok = cfg_set_log_file(cfg, log, optarg);
        got_log_file = true;
        break;
      case 'L':
        cfg->syslog = true;
        got_syslog = true;
        break;
      case 'h':
        help = true;
        break;
      case 'v':
        if (cfg->verbosity < 2)
          cfg->verbosity++;
        got_verbosity = true;
        break;
      default:
        is_ok = false;
        break;
    }
  }

  /* Parse config file. */
  if (is_ok)
    is_ok = cfg_parse_file(&cfg_file, log, cfg_file_path);

  /* Directly provided options supersede those from a config file. */
  if (!got_station)
    cfg->station = cfg_file.station;
  if (!got_base)
    cfg->base = cfg_file.base;
  if (!got_offset)
    cfg->offset = cfg_file.offset;
  if (!got_dut1)
    cfg->dut1 = cfg_file.dut1;
  if (!got_timeout)
    cfg->timeout = cfg_file.timeout;

#ifdef TSIG_HAVE_BACKENDS
  if (!got_backend)
    cfg->backend = cfg_file.backend;
#endif /* TSIG_HAVE_BACKENDS */

#ifdef TSIG_HAVE_ALSA
  if (!got_device)
    strcpy(cfg->device, cfg_file.device);
#endif /* TSIG_HAVE_ALSA */

  if (!got_format)
    cfg->format = cfg_file.format;
  if (!got_rate)
    cfg->rate = cfg_file.rate;
  if (!got_channels)
    cfg->channels = cfg_file.channels;
  if (!got_smooth)
    cfg->smooth = cfg_file.smooth;
  if (!got_ultrasound)
    cfg->ultrasound = cfg_file.ultrasound;
  if (!got_log_file)
    strcpy(cfg->log_file, cfg_file.log_file);
  if (!got_syslog)
    cfg->syslog = cfg_file.syslog;
  if (!got_verbosity)
    cfg->verbosity = cfg_file.verbosity;

  if (help || !is_ok)
    tsig_cfg_help();
  else
    tsig_log_finish_init(log, cfg->log_file, cfg->syslog, cfg->verbosity);

#ifdef TSIG_DEBUG
  cfg_print(&cfg_file, log);
  cfg_print(cfg, log);
#endif /* TSIG_DEBUG */

  return !is_ok ? TSIG_CFG_INIT_FAIL
         : help ? TSIG_CFG_INIT_HELP
                : TSIG_CFG_INIT_OK;
}

/** Print help message to stderr. */
void tsig_cfg_help(void) {
  fprintf(stderr, cfg_help_fmt);
}
