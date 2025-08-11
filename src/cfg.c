// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * cfg.c: Program configuration.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "cfg.h"
#include "defaults.h"

#include <alsa/asoundlib.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

#include <getopt.h>

/* Structure for matching a string to a value. */
typedef struct cfg_match {
  const char *str;
  const long value;
} cfg_match_t;

/** Default program configuration. */
static tsig_cfg_t cfg_default = {
    .offset = 0,
    .station = TSIG_CFG_STATION_WWVB,
    .dut1 = 0,
    .device = "default",
    .format = SND_PCM_FORMAT_S16,
    .rate = TSIG_CFG_RATE_48000,
    .channels = 1,
    .smooth = false,
    .ultrasound = false,
};

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
    "  -o, --offset=OFFSET      user offset in [+-]HH:mm:ss[.SSS] format\n"
    "  -d, --dut1=DUT1          DUT1 value in ms (only for MSF and WWVB)\n"
    "\n"
    "Sound options (rarely needed):\n"
    "  -D, --device=DEVICE      output ALSA device\n"
    "  -f, --format=FORMAT      output sample format\n"
    "  -r, --rate=RATE          output sample rate\n"
    "  -c, --channels=CHANNELS  output channels\n"
    "  -S, --smooth             smooth rapid gain changes in output waveform\n"
    "  -u, --ultrasound         enable ultrasound output (MAY DAMAGE EQUIPMENT)\n"
    "\n"
    "Miscellaneous:\n"
    "  -h, --help               show this help and exit\n"
    "\n"
    "Recognized option values (not all work on all systems):\n"
    "  time station   BPC, DCF77, JJY, JJY60, MSF, WWVB\n"
    "  user offset    -23:59:59.999 to 23:59:59.999\n"
    "  DUT1 value     -999 to 999\n"
    "  ALSA device    ALSA device name\n"
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
    "\n"
    "Default option values:\n"
    "  time station   WWVB\n"
    "  user offset    00:00:00.000\n"
    "  DUT1 value     0\n"
    "  ALSA device    default\n"
    "  sample format  S16\n"
    "  sample rate    48000\n"
    "  channels       1\n"
    "  smooth gain    off\n"
    "  ultrasound     off\n"
    "\n"
    /* clang-format on */
};

/** User offset limits (exclusive). */
static const long cfg_offset_min = -86400000;
static const long cfg_offset_max = 86400000;

/** Recognized time stations. */
static const cfg_match_t cfg_stations[] = {
    {"BPC", TSIG_CFG_STATION_BPC},     {"DCF77", TSIG_CFG_STATION_DCF77},
    {"JJY", TSIG_CFG_STATION_JJY},     {"JJY40", TSIG_CFG_STATION_JJY},
    {"JJY60", TSIG_CFG_STATION_JJY60}, {"MSF", TSIG_CFG_STATION_MSF},
    {"WWVB", TSIG_CFG_STATION_WWVB},   {NULL, 0},
};

/** DUT1 limits (exclusive). */
static const long cfg_dut1_min = -1000;
static const long cfg_dut1_max = 1000;

/** Recognized sample formats. */
static const cfg_match_t cfg_formats[] = {
    {"S16", SND_PCM_FORMAT_S16},
    {"S16_LE", SND_PCM_FORMAT_S16_LE},
    {"S16_BE", SND_PCM_FORMAT_S16_BE},
    {"S24", SND_PCM_FORMAT_S24},
    {"S24_LE", SND_PCM_FORMAT_S24_LE},
    {"S24_BE", SND_PCM_FORMAT_S24_BE},
    {"S32", SND_PCM_FORMAT_S32},
    {"S32_LE", SND_PCM_FORMAT_S32_LE},
    {"S32_BE", SND_PCM_FORMAT_S32_BE},
    {"U16", SND_PCM_FORMAT_U16},
    {"U16_LE", SND_PCM_FORMAT_U16_LE},
    {"U16_BE", SND_PCM_FORMAT_U16_BE},
    {"U24", SND_PCM_FORMAT_U24},
    {"U24_LE", SND_PCM_FORMAT_U24_LE},
    {"U24_BE", SND_PCM_FORMAT_U24_BE},
    {"U32", SND_PCM_FORMAT_U32},
    {"U32_LE", SND_PCM_FORMAT_U32_LE},
    {"U32_BE", SND_PCM_FORMAT_U32_BE},
    {"FLOAT", SND_PCM_FORMAT_FLOAT},
    {"FLOAT_LE", SND_PCM_FORMAT_FLOAT_LE},
    {"FLOAT_BE", SND_PCM_FORMAT_FLOAT_BE},
    {"FLOAT64", SND_PCM_FORMAT_FLOAT64},
    {"FLOAT64_LE", SND_PCM_FORMAT_FLOAT64_LE},
    {"FLOAT64_BE", SND_PCM_FORMAT_FLOAT64_BE},
    {NULL, 0},
};

/** Recognized sample rates.  */
static const cfg_match_t cfg_rates[] = {
    {"44100", TSIG_CFG_RATE_44100},
    {"48000", TSIG_CFG_RATE_48000},
    {"88200", TSIG_CFG_RATE_88200},
    {"96000", TSIG_CFG_RATE_96000},
    {"176400", TSIG_CFG_RATE_176400},
    {"192000", TSIG_CFG_RATE_192000},
    {"352800", TSIG_CFG_RATE_352800},
    {"384000", TSIG_CFG_RATE_384000},
    {NULL, 0},
};

/** Channel count limits (exclusive). */
static const long cfg_channels_min = 0;
static const long cfg_channels_max = 1024;

/** Time conversions. */
static const long cfg_msecs_hour = 3600000;
static const long cfg_msecs_min = 60000;
static const long cfg_msecs_sec = 1000;
static const long cfg_mins_hour = 60;
static const long cfg_secs_min = 60;

/** Long options. */
static struct option cfg_longopts[] = {
    {"station", required_argument, NULL, 's'},
    {"offset", required_argument, NULL, 'o'},
    {"dut1", required_argument, NULL, 'd'},
    {"device", required_argument, NULL, 'D'},
    {"format", required_argument, NULL, 'f'},
    {"rate", required_argument, NULL, 'r'},
    {"channels", required_argument, NULL, 'c'},
    {"smooth", no_argument, NULL, 'S'},
    {"ultrasound", no_argument, NULL, 'u'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0},
};

/** Match a string to a value. */
static bool cfg_match(const cfg_match_t cands[], const char *str,
                      long *out_value) {
  for (int i = 0; cands[i].str; i++) {
    /* strcasecmp() is non-standard, so do it ourselves. */
    const char *s1 = cands[i].str;
    const char *s2 = str;

    for (; *s1 && *s2; s1++, s2++)
      if ((*s1 <= 'Z' ? *s1 + 'a' - 'A' : *s1) !=
          (*s2 <= 'Z' ? *s2 + 'a' - 'A' : *s2))
        break;

    if (*s1 == *s2) {
      *out_value = cands[i].value;
      return true;
    }
  }

  return false;
}

/** Parse a string in [[[+-]HH:]mm:]ss[.SSS] format. */
static bool cfg_parse_offset(const char *str, long *out_msecs) {
  const char *l = NULL;
  const char *p = NULL;
  const char *r = NULL;
  int64_t mul = 100;
  int64_t msecs = 0;
  int colons = 0;

  /* Find the trimmed bounds of the string and the floating point. */
  for (const char *s = str; *s; s++) {
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

  /* Parse up to three digits from the right of the floating point. */
  if (p) {
    /* Floating point must be preceded or followed by a digit. */
    if (!(l < p && isdigit(*(p - 1))) && !(p + 1 < r && isdigit(*(p + 1))))
      return false;
    for (const char *s = p + 1; s < r; s++) {
      if (isdigit(*s)) {
        if (mul) {
          msecs += mul * (*s - '0');
          mul /= 10;
        }
      } else {
        return false;
      }
    }
  }

  /* Parse the rest of the string, checking for overflow. */
  mul = cfg_msecs_sec;

  for (const char *s = p ? p - 1 : r - 1; s >= l; s--) {
    if (*s == '+' || *s == '-') {
      /* Sign must be leftmost. */
      if (s != l)
        return false;
      if (*s == '-')
        msecs = -msecs;
    } else if (*s == ':') {
      /* Up to two colons, and they must be preceded by a digit. */
      if (l < s && isdigit(*(s - 1)) && colons < 2)
        mul = colons++ ? cfg_msecs_hour : cfg_msecs_min;
      else
        return false;
    } else if (isdigit(*s)) {
      msecs += mul * (*s - '0');
      mul *= 10;
      if (mul > INT_MAX)
        return false;
    } else {
      return false;
    }

    if (!(INT_MIN <= msecs && msecs <= INT_MAX))
      return false;
  }

  *out_msecs = (long)msecs;

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

/**
 * Initialize program configuration.
 *
 * @param cfg Uninitialized program configuration.
 * @param argc Command-line argument count.
 * @param argv Command-line argument vector.
 * @return Return code indicating whether initialization was successful,
 *  and if so, whether the user printed the help message.
 */
tsig_cfg_init_result_t tsig_cfg_init(tsig_cfg_t *cfg, int argc, char *argv[]) {
  bool has_error = false;
  bool help = false;
  long tmp;
  int opt;

  *cfg = cfg_default;

  while (!has_error) {
    opt = getopt_long(argc, argv, "s:o:d:D:f:r:c:Suh", cfg_longopts, NULL);
    if (opt < 0)
      break;

    has_error = true;

    /* TODO: Logging. */
    switch (opt) {
      case 'o':
        if (!cfg_parse_offset(optarg, &tmp)) {
          fprintf(stderr, "invalid offset \"%s\"\n", optarg);
        } else if (!(cfg_offset_min < tmp && tmp < cfg_offset_max)) {
          const char *sign = tmp < 0 ? "-" : "";
          if (tmp < 0)
            tmp = -tmp;
          fprintf(stderr,
                  "offset %s%.02ld:%.02ld:%.02ld.%.03ld must be between "
                  "-23:59:59.999 and 23:59:59.999\n",
                  sign, tmp / cfg_msecs_hour,
                  (tmp / cfg_msecs_min) % cfg_mins_hour,
                  (tmp / cfg_msecs_sec) % cfg_secs_min, tmp % cfg_msecs_sec);
        } else {
          cfg->offset = (int32_t)tmp;
          has_error = false;
        }
        break;
      case 's':
        if (!cfg_match(cfg_stations, optarg, &tmp)) {
          fprintf(stderr, "invalid station \"%s\"\n", optarg);
        } else {
          cfg->station = (tsig_cfg_station_t)tmp;
          has_error = false;
        }
        break;
      case 'd':
        if (!cfg_strtol(optarg, &tmp)) {
          fprintf(stderr, "invalid dut1 \"%s\"\n", optarg);
        } else if (!(cfg_dut1_min < tmp && tmp < cfg_dut1_max)) {
          fprintf(stderr, "dut1 %ld must be between %ld and %ld\n", tmp,
                  cfg_dut1_min + 1, cfg_dut1_max - 1);
        } else {
          cfg->dut1 = (int16_t)tmp;
          has_error = false;
        }
        break;
      case 'D':
        cfg->device = optarg;
        has_error = false;
        break;
      case 'f':
        if (!cfg_match(cfg_formats, optarg, &tmp)) {
          fprintf(stderr, "invalid format \"%s\"\n", optarg);
        } else {
          cfg->format = (snd_pcm_format_t)tmp;
          has_error = false;
        }
        break;
      case 'r':
        if (!cfg_match(cfg_rates, optarg, &tmp)) {
          fprintf(stderr, "invalid rate \"%s\"\n", optarg);
        } else {
          cfg->rate = (uint32_t)tmp;
          has_error = false;
        }
        break;
      case 'c':
        if (!cfg_strtol(optarg, &tmp)) {
          fprintf(stderr, "invalid channels \"%s\"\n", optarg);
        } else if (!(cfg_channels_min < tmp && tmp < cfg_channels_max)) {
          fprintf(stderr, "channels %ld must be between %ld and %ld\n", tmp,
                  cfg_channels_min + 1, cfg_channels_max - 1);
        } else {
          cfg->channels = (uint16_t)tmp;
          has_error = false;
        }
        break;
      case 'S':
        cfg->smooth = true;
        has_error = false;
        break;
      case 'u':
        cfg->ultrasound = true;
        has_error = false;
        break;
      case 'h':
        help = true;
        has_error = false;
        break;
      default:
        break;
    }
  }

  if (help || has_error)
    tsig_cfg_help();

  return has_error ? TSIG_CFG_INIT_FAIL
         : help    ? TSIG_CFG_INIT_HELP
                   : TSIG_CFG_INIT_OK;
}

/** Print help message to stderr. */
void tsig_cfg_help(void) {
  fprintf(stderr, cfg_help_fmt);
}
