// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * alsa.c: ALSA output facilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "alsa.h"

#include "audio.h"
#include "cfg.h"
#include "log.h"
#include "mapping.h"

#include <alsa/asoundlib.h>

#include <dlfcn.h>
#include <poll.h>
#include <unistd.h>

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/** ALSA library shared object name. */
static const char *alsa_lib_soname = "libasound.so.2";

/** ALSA library handle. */
static void *alsa_lib;

/** Pointers to ALSA library functions. */
/* clang-format off */
static const char *(*alsa_snd_pcm_access_name)(const snd_pcm_access_t _access);
static int (*alsa_snd_pcm_close)(snd_pcm_t *pcm);
static const char *(*alsa_snd_pcm_format_name)(const snd_pcm_format_t format);
static int (*alsa_snd_pcm_format_physical_width)(snd_pcm_format_t format);
static int (*alsa_snd_pcm_hw_params)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params);
static int (*alsa_snd_pcm_hw_params_any)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params);
static int (*alsa_snd_pcm_hw_params_get_buffer_size)(const snd_pcm_hw_params_t *params, snd_pcm_uframes_t *val);
static int (*alsa_snd_pcm_hw_params_get_period_size)(const snd_pcm_hw_params_t *params, snd_pcm_uframes_t *frames, int *dir);
static int (*alsa_snd_pcm_hw_params_set_access)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_access_t _access);
static int (*alsa_snd_pcm_hw_params_set_buffer_time_near)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val, int *dir);
static int (*alsa_snd_pcm_hw_params_set_channels_near)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val);
static int (*alsa_snd_pcm_hw_params_set_format)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_format_t val);
static int (*alsa_snd_pcm_hw_params_set_period_time_near)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val, int *dir);
static int (*alsa_snd_pcm_hw_params_set_rate_near)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val, int *dir);
static size_t (*alsa_snd_pcm_hw_params_sizeof)(void);
static int (*alsa_snd_pcm_open)(snd_pcm_t **pcm, const char *name, snd_pcm_stream_t stream, int mode);
static int (*alsa_snd_pcm_poll_descriptors)(snd_pcm_t *pcm, struct pollfd *pfds, unsigned int space);
static int (*alsa_snd_pcm_poll_descriptors_count)(snd_pcm_t *pcm);
static int (*alsa_snd_pcm_poll_descriptors_revents)(snd_pcm_t *pcm, struct pollfd *pfds, unsigned int nfds, unsigned short *revents);
static int (*alsa_snd_pcm_prepare)(snd_pcm_t *pcm);
static int (*alsa_snd_pcm_resume)(snd_pcm_t *pcm);
static snd_pcm_state_t (*alsa_snd_pcm_state)(snd_pcm_t *pcm);
static int (*alsa_snd_pcm_sw_params)(snd_pcm_t *pcm, snd_pcm_sw_params_t *params);
static int (*alsa_snd_pcm_sw_params_current)(snd_pcm_t *pcm, snd_pcm_sw_params_t *params);
static int (*alsa_snd_pcm_sw_params_get_boundary)(const snd_pcm_sw_params_t *params, snd_pcm_uframes_t *val);
static int (*alsa_snd_pcm_sw_params_set_avail_min)(snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val);
static int (*alsa_snd_pcm_sw_params_set_start_threshold)(snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val);
static int (*alsa_snd_pcm_sw_params_set_stop_threshold)(snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val);
static size_t (*alsa_snd_pcm_sw_params_sizeof)(void);
static snd_pcm_sframes_t (*alsa_snd_pcm_writei)(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size);
static const char *(*alsa_snd_strerror)(int errnum);
/* clang-format on */

/* Signal status flag. */
static volatile sig_atomic_t alsa_got_signal = 0;

/** Default buffer time in us. */
static const unsigned alsa_buffer_time = 200000;

/** Default period time in us. */
static const unsigned alsa_period_time = 100000;

/** Sample format map. */
static const tsig_mapping_nn_t alsa_format_map[] = {
    {TSIG_AUDIO_FORMAT_S16, SND_PCM_FORMAT_S16},
    {TSIG_AUDIO_FORMAT_S16_LE, SND_PCM_FORMAT_S16_LE},
    {TSIG_AUDIO_FORMAT_S16_BE, SND_PCM_FORMAT_S16_BE},
    {TSIG_AUDIO_FORMAT_S24, SND_PCM_FORMAT_S24},
    {TSIG_AUDIO_FORMAT_S24_LE, SND_PCM_FORMAT_S24_LE},
    {TSIG_AUDIO_FORMAT_S24_BE, SND_PCM_FORMAT_S24_BE},
    {TSIG_AUDIO_FORMAT_S32, SND_PCM_FORMAT_S32},
    {TSIG_AUDIO_FORMAT_S32_LE, SND_PCM_FORMAT_S32_LE},
    {TSIG_AUDIO_FORMAT_S32_BE, SND_PCM_FORMAT_S32_BE},
    {TSIG_AUDIO_FORMAT_U16, SND_PCM_FORMAT_U16},
    {TSIG_AUDIO_FORMAT_U16_LE, SND_PCM_FORMAT_U16_LE},
    {TSIG_AUDIO_FORMAT_U16_BE, SND_PCM_FORMAT_U16_BE},
    {TSIG_AUDIO_FORMAT_U24, SND_PCM_FORMAT_U24},
    {TSIG_AUDIO_FORMAT_U24_LE, SND_PCM_FORMAT_U24_LE},
    {TSIG_AUDIO_FORMAT_U24_BE, SND_PCM_FORMAT_U24_BE},
    {TSIG_AUDIO_FORMAT_U32, SND_PCM_FORMAT_U32},
    {TSIG_AUDIO_FORMAT_U32_LE, SND_PCM_FORMAT_U32_LE},
    {TSIG_AUDIO_FORMAT_U32_BE, SND_PCM_FORMAT_U32_BE},
    {TSIG_AUDIO_FORMAT_FLOAT, SND_PCM_FORMAT_FLOAT},
    {TSIG_AUDIO_FORMAT_FLOAT_LE, SND_PCM_FORMAT_FLOAT_LE},
    {TSIG_AUDIO_FORMAT_FLOAT_BE, SND_PCM_FORMAT_FLOAT_BE},
    {TSIG_AUDIO_FORMAT_FLOAT64, SND_PCM_FORMAT_FLOAT64},
    {TSIG_AUDIO_FORMAT_FLOAT64_LE, SND_PCM_FORMAT_FLOAT64_LE},
    {TSIG_AUDIO_FORMAT_FLOAT64_BE, SND_PCM_FORMAT_FLOAT64_BE},
    {0, 0},
};

/** Signal handler. */
static void alsa_signal_handler(int signal) {
  (void)signal; /* Suppress unused parameter warning. */
  alsa_got_signal = 1;
}

/** Sample format lookup. */
static snd_pcm_format_t alsa_format(const tsig_audio_format_t format) {
  snd_pcm_format_t value = tsig_mapping_nn_match_key(alsa_format_map, format);
  return value < 0 ? SND_PCM_FORMAT_UNKNOWN : value;
}

/** Set hardware parameters. */
static int alsa_set_hw_params(tsig_alsa_t *alsa, tsig_cfg_t *cfg) {
  tsig_log_t *log = alsa->log;
  snd_pcm_hw_params_t *params;
  snd_pcm_t *pcm = alsa->pcm;
  snd_pcm_format_t format;
  snd_pcm_uframes_t size;
  unsigned val;
  int err;

  /* snd_pcm_hw_params_alloca(&params); */
  params = __builtin_alloca(alsa_snd_pcm_hw_params_sizeof());
  memset(params, 0, alsa_snd_pcm_hw_params_sizeof());

  err = alsa_snd_pcm_hw_params_any(pcm, params);
  if (err < 0) {
    tsig_log_err("failed to get hw params: %s", alsa_snd_strerror(err));
    return err;
  }

  err = alsa_snd_pcm_hw_params_set_access(pcm, params,
                                          SND_PCM_ACCESS_RW_INTERLEAVED);
  if (err < 0) {
    tsig_log_err("failed to set access %s: %s",
                 alsa_snd_pcm_access_name(SND_PCM_ACCESS_RW_INTERLEAVED),
                 alsa_snd_strerror(err));
    return err;
  }
  alsa->access = SND_PCM_ACCESS_RW_INTERLEAVED;

  format = alsa_format(cfg->format);
  err = alsa_snd_pcm_hw_params_set_format(pcm, params, format);
  if (err < 0) {
    for (int i = 0; alsa_format_map[i].value; i++) {
      snd_pcm_format_t cand = alsa_format_map[i].value;
      if (!alsa_snd_pcm_hw_params_set_format(pcm, params, cand)) {
        tsig_log_note("failed to set format %s, fallback to %s",
                      alsa_snd_pcm_format_name(format),
                      alsa_snd_pcm_format_name(cand));
        format = cand;
        err = 0;
        break;
      }
    }
  }
  if (err < 0) {
    tsig_log_err("failed to set format %s: %s",
                 alsa_snd_pcm_format_name(format), alsa_snd_strerror(err));
    return err;
  }
  alsa->format = format;

  val = cfg->rate;
  err = alsa_snd_pcm_hw_params_set_rate_near(pcm, params, &val, NULL);
  if (val != cfg->rate) {
    if (TSIG_AUDIO_RATE_44100 <= val && val <= TSIG_AUDIO_RATE_384000)
      tsig_log_note("failed to set rate near %" PRIu32 ", fallback to %u",
                    cfg->rate, val);
    else
      err = -EINVAL;
  }
  if (err < 0) {
    tsig_log_err("failed to set rate near %" PRIu32 ": %s", cfg->rate,
                 alsa_snd_strerror(err));
    return err;
  }
  alsa->rate = val;

  val = cfg->channels;
  err = alsa_snd_pcm_hw_params_set_channels_near(pcm, params, &val);
  if (val != cfg->channels)
    tsig_log_note("failed to set channels near %" PRIu16 ", fallback to %u",
                  cfg->channels, val);
  if (err < 0) {
    tsig_log_err("failed to set channels near %" PRIu16 ": %s", cfg->channels,
                 alsa_snd_strerror(err));
    return err;
  }
  alsa->channels = val;

  val = alsa_buffer_time;
  err = alsa_snd_pcm_hw_params_set_buffer_time_near(pcm, params, &val, NULL);
  if (err < 0) {
    tsig_log_err("failed to set buffer time near %u: %s", alsa_buffer_time,
                 alsa_snd_strerror(err));
    return err;
  }
  err = alsa_snd_pcm_hw_params_get_buffer_size(params, &size);
  if (err < 0) {
    tsig_log_err("failed to get buffer size: %s", alsa_snd_strerror(err));
    return err;
  }
  alsa->buffer_size = size;

  val = alsa_period_time;
  err = alsa_snd_pcm_hw_params_set_period_time_near(pcm, params, &val, NULL);
  if (err < 0) {
    tsig_log_err("failed to set period time near %u: %s", alsa_period_time,
                 alsa_snd_strerror(err));
    return err;
  }
  err = alsa_snd_pcm_hw_params_get_period_size(params, &size, NULL);
  if (err < 0) {
    tsig_log_err("failed to get period size: %s", alsa_snd_strerror(err));
    return err;
  }
  alsa->period_size = size;

  err = alsa_snd_pcm_hw_params(pcm, params);
  if (err < 0) {
    tsig_log_err("failed to set hw params: %s", alsa_snd_strerror(err));
    return err;
  }

  return 0;
}

/** Set software parameters. */
static int alsa_set_sw_params(tsig_alsa_t *alsa) {
  tsig_log_t *log = alsa->log;
  snd_pcm_sw_params_t *params;
  snd_pcm_t *pcm = alsa->pcm;
  snd_pcm_uframes_t val;
  int err;

  /* snd_pcm_sw_params_alloca(&params); */
  params = __builtin_alloca(alsa_snd_pcm_sw_params_sizeof());
  memset(params, 0, alsa_snd_pcm_sw_params_sizeof());

  err = alsa_snd_pcm_sw_params_current(pcm, params);
  if (err < 0) {
    tsig_log_err("failed to get sw params: %s", alsa_snd_strerror(err));
    return err;
  }

  /* Start playback when the buffer contains the most possible whole periods. */
  val = (alsa->buffer_size / alsa->period_size) * alsa->period_size;
  err = alsa_snd_pcm_sw_params_set_start_threshold(pcm, params, val);
  if (err < 0) {
    tsig_log_err("failed to set start threshold %lu: %s", val,
                 alsa_snd_strerror(err));
    return err;
  }
  alsa->start_threshold = val;

  /* Accept more samples when the buffer is >=1 period from being full. */
  err = alsa_snd_pcm_sw_params_set_avail_min(pcm, params, alsa->period_size);
  if (err < 0) {
    tsig_log_err("failed to set avail min: %s", alsa_snd_strerror(err));
    return err;
  }
  alsa->avail_min = alsa->period_size;

  /*
   * Setting the stop threshold to the boundary keeps the device from stopping
   * during a buffer underrun (it loops the existing buffer contents instead).
   * We can keep providing samples and playback will eventually be resynced
   * as though the underrun had never occurred, which is the behavior we want.
   */

  err = alsa_snd_pcm_sw_params_get_boundary(params, &val);
  if (err < 0) {
    tsig_log_err("failed to get boundary: %s", alsa_snd_strerror(err));
    return err;
  }
  err = alsa_snd_pcm_sw_params_set_stop_threshold(pcm, params, val);
  if (err < 0) {
    tsig_log_err("failed to set stop threshold %lu: %s", val,
                 alsa_snd_strerror(err));
    return err;
  }

  err = alsa_snd_pcm_sw_params(pcm, params);
  if (err < 0) {
    tsig_log_err("failed to set sw params: %s", alsa_snd_strerror(err));
    return err;
  }

  return 0;
}

/** Attempt to recover from buffer underruns/overruns. */
static void alsa_xrun_recover(tsig_log_t *log, snd_pcm_t *pcm, int err) {
  /* Resume if device is suspended. */
  if (err == -ESTRPIPE) {
    tsig_log("recovering from suspend");
    while ((err = alsa_snd_pcm_resume(pcm)) == -EAGAIN)
      sleep(1);
  } else {
    tsig_log("recovering from underrun\n");
  }

  if (err < 0) {
    err = alsa_snd_pcm_prepare(pcm);
    if (err < 0)
      tsig_log_warn("failed to recover from xrun: %s", alsa_snd_strerror(err));
  }
}

/** Wait for poll. */
static int alsa_loop_wait(snd_pcm_t *pcm, struct pollfd *pfds, unsigned nfds) {
  unsigned short revents;
  snd_pcm_state_t state;

  for (;;) {
    if (poll(pfds, nfds, -1) < 0) {
      if (errno == EINTR && alsa_got_signal) {
        alsa_got_signal = 0;
        return -EINTR;
      }
    }

    alsa_snd_pcm_poll_descriptors_revents(pcm, pfds, nfds, &revents);

    if (revents & POLLERR) {
      state = alsa_snd_pcm_state(pcm);
      return state == SND_PCM_STATE_SUSPENDED ? -ESTRPIPE
             : state == SND_PCM_STATE_XRUN    ? -EPIPE
                                              : -EIO;
    }

    if (revents & POLLOUT)
      return 0;
  }
}

#ifdef TSIG_DEBUG
/** Print initialized ALSA output context. */
static void alsa_print(tsig_alsa_t *alsa) {
  const char *access = alsa_snd_pcm_access_name(alsa->access);
  const char *format = alsa_snd_pcm_format_name(alsa->format);
  const char *audio_format = tsig_audio_format_name(alsa->audio_format);
  tsig_log_t *log = alsa->log;
  tsig_log_dbg("tsig_alsa_t %p = {", alsa);
  tsig_log_dbg("  .pcm             = %p,", alsa->pcm);
  tsig_log_dbg("  .device          = \"%s\",", alsa->device);
  tsig_log_dbg("  .access          = %s,", access);
  tsig_log_dbg("  .format          = %s,", format);
  tsig_log_dbg("  .rate            = %u,", alsa->rate);
  tsig_log_dbg("  .channels        = %u,", alsa->channels);
  tsig_log_dbg("  .buffer_size     = %lu,", alsa->buffer_size);
  tsig_log_dbg("  .period_size     = %lu,", alsa->period_size);
  tsig_log_dbg("  .start_threshold = %lu,", alsa->start_threshold);
  tsig_log_dbg("  .avail_min       = %lu,", alsa->avail_min);
  tsig_log_dbg("  .audio_format    = %s,", audio_format);
  tsig_log_dbg("  .timeout         = %u,", alsa->timeout);
  tsig_log_dbg("  .log             = %p,", alsa->log);
  tsig_log_dbg("};");
}
#endif /* TSIG_DEBUG */

/**
 * Initialize ALSA output.
 *
 * @param log Initialized logging context.
 * @return 0 upon success, negative error code upon error.
 */
int tsig_alsa_lib_init(tsig_log_t *log) {
  alsa_lib = dlopen(alsa_lib_soname, RTLD_LAZY);
  if (!alsa_lib) {
    tsig_log_err("failed to load ALSA library: %s", dlerror());
    return -EINVAL;
  }

#define alsa_dlsym_assign(f)                                          \
  do {                                                                \
    *(void **)(&alsa_##f) = dlsym(alsa_lib, #f);                      \
    if (!alsa_##f) {                                                  \
      tsig_log_err("failed to load ALSA library function %s: %s", #f, \
                   dlerror());                                        \
      return -EINVAL;                                                 \
    }                                                                 \
  } while (0)

  alsa_dlsym_assign(snd_pcm_access_name);
  alsa_dlsym_assign(snd_pcm_close);
  alsa_dlsym_assign(snd_pcm_format_name);
  alsa_dlsym_assign(snd_pcm_format_physical_width);
  alsa_dlsym_assign(snd_pcm_hw_params);
  alsa_dlsym_assign(snd_pcm_hw_params_any);
  alsa_dlsym_assign(snd_pcm_hw_params_get_buffer_size);
  alsa_dlsym_assign(snd_pcm_hw_params_get_period_size);
  alsa_dlsym_assign(snd_pcm_hw_params_set_access);
  alsa_dlsym_assign(snd_pcm_hw_params_set_buffer_time_near);
  alsa_dlsym_assign(snd_pcm_hw_params_set_channels_near);
  alsa_dlsym_assign(snd_pcm_hw_params_set_format);
  alsa_dlsym_assign(snd_pcm_hw_params_set_period_time_near);
  alsa_dlsym_assign(snd_pcm_hw_params_set_rate_near);
  alsa_dlsym_assign(snd_pcm_hw_params_sizeof);
  alsa_dlsym_assign(snd_pcm_open);
  alsa_dlsym_assign(snd_pcm_poll_descriptors);
  alsa_dlsym_assign(snd_pcm_poll_descriptors_count);
  alsa_dlsym_assign(snd_pcm_poll_descriptors_revents);
  alsa_dlsym_assign(snd_pcm_prepare);
  alsa_dlsym_assign(snd_pcm_resume);
  alsa_dlsym_assign(snd_pcm_state);
  alsa_dlsym_assign(snd_pcm_sw_params);
  alsa_dlsym_assign(snd_pcm_sw_params_current);
  alsa_dlsym_assign(snd_pcm_sw_params_get_boundary);
  alsa_dlsym_assign(snd_pcm_sw_params_set_avail_min);
  alsa_dlsym_assign(snd_pcm_sw_params_set_start_threshold);
  alsa_dlsym_assign(snd_pcm_sw_params_set_stop_threshold);
  alsa_dlsym_assign(snd_pcm_sw_params_sizeof);
  alsa_dlsym_assign(snd_pcm_writei);
  alsa_dlsym_assign(snd_strerror);

#undef alsa_dlsym_assign

  return 0;
}

/**
 * Initialize ALSA output context.
 *
 * @param alsa Uninitialized ALSA output context.
 * @param cfg Initialized program configuration.
 * @param log Initialized logging context.
 * @return 0 upon success, negative error code upon error.
 */
int tsig_alsa_init(tsig_alsa_t *alsa, tsig_cfg_t *cfg, tsig_log_t *log) {
  snd_pcm_t *pcm;
  int err;

  alsa->timeout = cfg->timeout;
  alsa->log = log;

  err = alsa_snd_pcm_open(&pcm, cfg->device, SND_PCM_STREAM_PLAYBACK, 0);
  if (err < 0) {
    tsig_log_err("failed to open ALSA device %s: %s", cfg->device,
                 alsa_snd_strerror(err));
    return err;
  }
  alsa->pcm = pcm;
  alsa->device = cfg->device;

  err = alsa_set_hw_params(alsa, cfg);
  if (err < 0)
    goto out_deinit;

  alsa->audio_format =
      tsig_mapping_nn_match_value(alsa_format_map, alsa->format);

  err = alsa_set_sw_params(alsa);
  if (err < 0)
    goto out_deinit;

#ifndef TSIG_DEBUG
  tsig_log_dbg(
      "opened ALSA device \"%s\" %s %u Hz %uch, buffer %lu, period %lu",
      alsa->device, alsa_snd_pcm_format_name(alsa->format), alsa->rate,
      alsa->channels, alsa->buffer_size, alsa->period_size);
#else
  alsa_print(alsa);
#endif /* TSIG_DEBUG */

  return 0;

out_deinit:
  /* We lose errors from tsig_alsa_deinit(), but that's too bad. */
  tsig_alsa_deinit(alsa);
  return err;
}

/**
 * ALSA output loop.
 *
 * @param alsa Initialized ALSA output context.
 * @param cb Sample generator callback function.
 * @param cb_data Callback function context object.
 * @return 0 if loop exited normally, negative error code upon error.
 */
int tsig_alsa_loop(tsig_alsa_t *alsa, tsig_audio_cb_t cb, void *cb_data) {
  int phys_width = alsa_snd_pcm_format_physical_width(alsa->format) / CHAR_BIT;
  struct sigaction sa = {.sa_handler = &alsa_signal_handler};
  tsig_log_t *log = alsa->log;
  snd_pcm_t *pcm = alsa->pcm;
  struct pollfd *pfds = NULL;
  snd_pcm_uframes_t written;
  snd_pcm_uframes_t remain;
  struct sigaction sa_alrm;
  struct sigaction sa_term;
  struct sigaction sa_int;
  bool is_running = false;
  double *cb_buf = NULL;
  uint8_t *buf = NULL;
  uint8_t *ptr;
  int nfds;
  int err;

  nfds = alsa_snd_pcm_poll_descriptors_count(pcm);
  if (nfds <= 0) {
    tsig_log_err("failed to get poll descriptors count");
    return nfds;
  }

  pfds = malloc(sizeof(*pfds) * nfds);
  if (!pfds) {
    tsig_log_err("failed to allocate poll descriptors requests");
    return -ENOMEM;
  }

  err = alsa_snd_pcm_poll_descriptors(pcm, pfds, nfds);
  if (err < 0) {
    tsig_log_err("failed to get poll descriptors: %s", alsa_snd_strerror(err));
    goto out_free_bufs;
  }

  cb_buf = malloc(sizeof(*cb_buf) * alsa->period_size);
  if (!cb_buf) {
    tsig_log_err("failed to allocate generated sample buffer");
    err = -ENOMEM;
    goto out_free_bufs;
  }

  buf = malloc(sizeof(*buf) * alsa->period_size * alsa->channels *
               alsa_snd_pcm_format_physical_width(alsa->format));
  if (!buf) {
    tsig_log_err("failed to allocate period buffer");
    err = -ENOMEM;
    goto out_free_bufs;
  }

  /* Install signal handler and set user timeout. */
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, &sa_int);
  sigaction(SIGTERM, &sa, &sa_term);
  sigaction(SIGALRM, &sa, &sa_alrm);
  alarm(alsa->timeout);

  /*
   * ALSA pulls one period's samples at a time with up to two waits.
   * cf. alsa-lib, test/pcm.c
   */

  for (;;) {
    if (is_running) {
      err = alsa_loop_wait(pcm, pfds, nfds);
      if (err == -EINTR) {
        err = 0;
        goto out_restore_signals;
      } else if (err == -EIO) {
        tsig_log_err("failed to wait for poll: %s", alsa_snd_strerror(err));
        goto out_restore_signals;
      } else if (err < 0) {
        alsa_xrun_recover(log, pcm, err);
        is_running = false;
      }
    }

    /* Generate one period's worth of 1ch 64-bit float samples. */
    cb(cb_data, cb_buf, alsa->period_size);

    /* Fill the period buffer with the generated samples. */
    tsig_audio_fill_buffer(alsa->audio_format, alsa->channels,
                           alsa->period_size, buf, cb_buf);

    /* Write the generated samples to the output device. */
    remain = alsa->period_size;
    ptr = buf;

    while (remain) {
      err = alsa_snd_pcm_writei(pcm, ptr, remain);
      if (err == -EBADFD) {
        tsig_log_err("failed to write frames: %s", alsa_snd_strerror(err));
        goto out_restore_signals;
      } else if (err < 0) {
        alsa_xrun_recover(log, pcm, err);
        is_running = false;
        break; /* Skip one period. */
      }

      if (alsa_snd_pcm_state(pcm) == SND_PCM_STATE_RUNNING)
        is_running = true;

      written = (snd_pcm_uframes_t)err;
      ptr += written * alsa->channels * phys_width;
      remain -= written;
      if (!remain)
        break;

      err = alsa_loop_wait(pcm, pfds, nfds);
      if (err == -EINTR) {
        err = 0;
        goto out_restore_signals;
      } else if (err == -EIO) {
        tsig_log_err("failed to wait for poll: %s", alsa_snd_strerror(err));
        goto out_restore_signals;
      } else if (err < 0) {
        alsa_xrun_recover(log, pcm, err);
        is_running = false;
      }
    }
  }

out_restore_signals:
  sigaction(SIGALRM, &sa_alrm, NULL);
  sigaction(SIGTERM, &sa_term, NULL);
  sigaction(SIGINT, &sa_int, NULL);
  alarm(0);

out_free_bufs:
  free(buf);
  free(cb_buf);
  free(pfds);

  return err;
}

/**
 * Deinitialize ALSA output context.
 *
 * @param alsa Initialized ALSA output context.
 * @return 0 upon success, negative error code upon error.
 */
int tsig_alsa_deinit(tsig_alsa_t *alsa) {
  tsig_log_t *log = alsa->log;
  int err;

  err = alsa_snd_pcm_close(alsa->pcm);
  if (err < 0)
    tsig_log_err("failed to close ALSA device %s: %s", alsa->device,
                 alsa_snd_strerror(err));

  return err;
}

/**
 * Deinitialize ALSA output.
 *
 * @param log Initialized logging context.
 * @return 0 upon success, negative error code upon error.
 */
int tsig_alsa_lib_deinit(tsig_log_t *log) {
  if (!dlclose(alsa_lib))
    return 0;

  tsig_log_err("failed to unload ALSA library: %s", dlerror());

  return -EINVAL;
}
