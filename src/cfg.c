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
static bool cfg_set_offset(tsig_cfg_t *cfg, tsig_log_t *log, const char *str);
static bool cfg_set_dut1(tsig_cfg_t *cfg, tsig_log_t *log, const char *str);

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

/** User offset limits (exclusive). */
static const long cfg_offset_min = -86400000;
static const long cfg_offset_max = 86400000;

/** DUT1 limits (exclusive). */
static const long cfg_dut1_min = -1000;
static const long cfg_dut1_max = 1000;

/** Channel count limits (exclusive). */
static const long cfg_channels_min = 0;
static const long cfg_channels_max = 1024;

/** Time conversions. */
static const long cfg_msecs_hour = 3600000;
static const long cfg_msecs_min = 60000;
static const long cfg_msecs_sec = 1000;
static const long cfg_mins_hour = 60;
static const long cfg_secs_min = 60;

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
    "  user offset    -23:59:59.999 to 23:59:59.999\n"
    "  DUT1 value     -999 to 999\n"

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
    "  user offset    00:00:00.000\n"
    "  DUT1 value     0\n"

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
    .offset = 0,
    .dut1 = 0,

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
    {"offset", required_argument, NULL, 'o'},
    {"dut1", required_argument, NULL, 'd'},

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
    "s:o:d:"

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
    {"offset", &cfg_set_offset},
    {"dut1", &cfg_set_dut1},

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

/** Setter for offset. */
static bool cfg_set_offset(tsig_cfg_t *cfg, tsig_log_t *log, const char *str) {
  long offset;

  if (!cfg_parse_offset(str, &offset)) {
    tsig_log_err("invalid offset \"%s\"", str);
    return false;
  }

  if (!(cfg_offset_min < offset && offset < cfg_offset_max)) {
    const char *sign = offset < 0 ? "-" : "";
    if (offset < 0)
      offset = -offset;
    tsig_log_err(
        "offset %s%.02ld:%.02ld:%.02ld.%.03ld must be between "
        "-23:59:59.999 and 23:59:59.999",
        sign, offset / cfg_msecs_hour, (offset / cfg_msecs_min) % cfg_mins_hour,
        (offset / cfg_msecs_sec) % cfg_secs_min, offset % cfg_msecs_sec);
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
  tsig_log_dbg("  .offset     = %d,", cfg->offset);
  tsig_log_dbg("  .dut1       = %hd,", cfg->dut1);

#ifdef TSIG_HAVE_BACKENDS
  tsig_log_dbg("  .backend    = %s,", backend);
#endif /* TSIG_HAVE_BACKENDS */

#ifdef TSIG_HAVE_ALSA
  tsig_log_dbg("  .device     = \"%s\",", cfg->device);
#endif /* TSIG_HAVE_ALSA */

  tsig_log_dbg("  .format     = %s,", format);
  tsig_log_dbg("  .rate       = %u,", cfg->rate);
  tsig_log_dbg("  .channels   = %hu,", cfg->channels);
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
  bool got_offset = false;
  bool got_dut1 = false;

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
      case 'o':
        is_ok = cfg_set_offset(cfg, log, optarg);
        got_offset = true;
        break;
      case 'd':
        is_ok = cfg_set_dut1(cfg, log, optarg);
        got_dut1 = true;
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
  if (!got_offset)
    cfg->offset = cfg_file.offset;
  if (!got_dut1)
    cfg->dut1 = cfg_file.dut1;

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
