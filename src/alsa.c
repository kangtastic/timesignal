// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * alsa.c: Sound output facilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "alsa.h"
#include "cfg.h"

#include <alsa/asoundlib.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

#include <poll.h>

/** Default buffer time in us. */
static const unsigned alsa_buffer_time = 200000;

/** Default period time in us. */
static const unsigned alsa_period_time = 100000;

/** Fallback sample formats. */
static const snd_pcm_format_t alsa_format[] = {
    SND_PCM_FORMAT_S16_LE,     SND_PCM_FORMAT_S16_BE,
    SND_PCM_FORMAT_S24_LE,     SND_PCM_FORMAT_S24_BE,
    SND_PCM_FORMAT_S32_LE,     SND_PCM_FORMAT_S32_BE,
    SND_PCM_FORMAT_U16_LE,     SND_PCM_FORMAT_U16_BE,
    SND_PCM_FORMAT_U24_LE,     SND_PCM_FORMAT_U24_BE,
    SND_PCM_FORMAT_U32_LE,     SND_PCM_FORMAT_U32_BE,
    SND_PCM_FORMAT_FLOAT_LE,   SND_PCM_FORMAT_FLOAT_BE,
    SND_PCM_FORMAT_FLOAT64_LE, SND_PCM_FORMAT_FLOAT64_BE,
    SND_PCM_FORMAT_LAST,
};

/** Set hardware parameters. */
static int alsa_set_hw_params(tsig_alsa_t *alsa, tsig_cfg_t *cfg) {
  tsig_log_t *log = alsa->log;
  snd_pcm_hw_params_t *params;
  snd_pcm_t *pcm = alsa->pcm;
  snd_pcm_format_t format;
  snd_pcm_uframes_t size;
  unsigned val;
  int err;

  snd_pcm_hw_params_alloca(&params);

  err = snd_pcm_hw_params_any(pcm, params);
  if (err < 0) {
    tsig_log_err("failed to get hw params: %s", snd_strerror(err));
    return err;
  }

  err =
      snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
  if (err < 0) {
    tsig_log_err("failed to set access %s: %s",
                 snd_pcm_access_name(SND_PCM_ACCESS_RW_INTERLEAVED),
                 snd_strerror(err));
    return err;
  }
  alsa->access = SND_PCM_ACCESS_RW_INTERLEAVED;

  format = cfg->format;
  err = snd_pcm_hw_params_set_format(pcm, params, format);
  if (err < 0) {
    for (int i = 0; alsa_format[i] != SND_PCM_FORMAT_LAST; i++) {
      if (!snd_pcm_hw_params_set_format(pcm, params, alsa_format[i])) {
        tsig_log_note("failed to set format %s, fallback to %s",
                      snd_pcm_format_name(format),
                      snd_pcm_format_name(alsa_format[i]));
        format = alsa_format[i];
        err = 0;
        break;
      }
    }
  }
  if (err < 0) {
    tsig_log_err("failed to set format %s: %s", snd_pcm_format_name(format),
                 snd_strerror(err));
    return err;
  }
  alsa->format = format;

  val = cfg->rate;
  err = snd_pcm_hw_params_set_rate_near(pcm, params, &val, NULL);
  if (val != cfg->rate) {
    if (TSIG_CFG_RATE_44100 <= val && val <= TSIG_CFG_RATE_384000)
      tsig_log_note("failed to set rate near %u, fallback to %u", cfg->rate,
                    val);
    else
      err = -EINVAL;
  }
  if (err < 0) {
    tsig_log_err("failed to set rate near %u: %s", cfg->rate,
                 snd_strerror(err));
    return err;
  }
  alsa->rate = val;

  val = cfg->channels;
  err = snd_pcm_hw_params_set_channels_near(pcm, params, &val);
  if (val != cfg->channels)
    tsig_log_note("failed to set channels near %hu, fallback to %u",
                  cfg->channels, val);
  if (err < 0) {
    tsig_log_err("failed to set channels near %hu: %s", cfg->channels,
                 snd_strerror(err));
    return err;
  }
  alsa->channels = val;

  val = alsa_buffer_time;
  err = snd_pcm_hw_params_set_buffer_time_near(pcm, params, &val, NULL);
  if (err < 0) {
    tsig_log_err("failed to set buffer time near %u: %s", alsa_buffer_time,
                 snd_strerror(err));
    return err;
  }
  err = snd_pcm_hw_params_get_buffer_size(params, &size);
  if (err < 0) {
    tsig_log_err("failed to get buffer size: %s", snd_strerror(err));
    return err;
  }
  alsa->buffer_size = size;

  val = alsa_period_time;
  err = snd_pcm_hw_params_set_period_time_near(pcm, params, &val, NULL);
  if (err < 0) {
    tsig_log_err("failed to set period time near %u: %s", alsa_period_time,
                 snd_strerror(err));
    return err;
  }
  err = snd_pcm_hw_params_get_period_size(params, &size, NULL);
  if (err < 0) {
    tsig_log_err("failed to get period size: %s", snd_strerror(err));
    return err;
  }
  alsa->period_size = size;

  err = snd_pcm_hw_params(pcm, params);
  if (err < 0) {
    tsig_log_err("failed to set hw params: %s", snd_strerror(err));
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

  snd_pcm_sw_params_alloca(&params);

  err = snd_pcm_sw_params_current(pcm, params);
  if (err < 0) {
    tsig_log_err("failed to get sw params: %s", snd_strerror(err));
    return err;
  }

  /* Start playback when the buffer contains the most possible whole periods. */
  val = (alsa->buffer_size / alsa->period_size) * alsa->period_size;
  err = snd_pcm_sw_params_set_start_threshold(pcm, params, val);
  if (err < 0) {
    tsig_log_err("failed to set start threshold %lu: %s", val,
                 snd_strerror(err));
    return err;
  }
  alsa->start_threshold = val;

  /* Accept more samples when the buffer is >=1 period from being full. */
  err = snd_pcm_sw_params_set_avail_min(pcm, params, alsa->period_size);
  if (err < 0) {
    tsig_log_err("failed to set avail min: %s", snd_strerror(err));
    return err;
  }
  alsa->avail_min = alsa->period_size;

  /*
   * Setting the stop threshold to the boundary keeps the device from stopping
   * during a buffer underrun (it loops the existing buffer contents instead).
   * We can keep providing samples and playback will eventually be resynced
   * as though the underrun had never occurred, which is the behavior we want.
   */

  err = snd_pcm_sw_params_get_boundary(params, &val);
  if (err < 0) {
    tsig_log_err("failed to get boundary: %s", snd_strerror(err));
    return err;
  }
  err = snd_pcm_sw_params_set_stop_threshold(pcm, params, val);
  if (err < 0) {
    tsig_log_err("failed to set stop threshold %lu: %s", val,
                 snd_strerror(err));
    return err;
  }

  err = snd_pcm_sw_params(pcm, params);
  if (err < 0) {
    tsig_log_err("failed to set sw params: %s", snd_strerror(err));
    return err;
  }

  return 0;
}

/** Attempt to recover from buffer underruns/overruns. */
static void alsa_xrun_recover(tsig_log_t *log, snd_pcm_t *pcm, int err) {
  /* Resume if device is suspended. */
  if (err == -ESTRPIPE) {
    tsig_log("recovering from suspend");
    while ((err = snd_pcm_resume(pcm)) == -EAGAIN)
      sleep(1);
  } else {
    tsig_log("recovering from underrun\n");
  }

  if (err < 0) {
    err = snd_pcm_prepare(pcm);
    if (err < 0)
      tsig_log_warn("failed to recover from xrun: %s", snd_strerror(err));
  }
}

/** Wait for poll. */
static int alsa_loop_wait(snd_pcm_t *pcm, struct pollfd *pfds, unsigned nfds) {
  unsigned short revents;
  snd_pcm_state_t state;

  for (;;) {
    poll(pfds, nfds, -1);
    snd_pcm_poll_descriptors_revents(pcm, pfds, nfds, &revents);

    if (revents & POLLERR) {
      state = snd_pcm_state(pcm);
      return state == SND_PCM_STATE_SUSPENDED ? -ESTRPIPE
             : state == SND_PCM_STATE_XRUN    ? -EPIPE
                                              : -EIO;
    }

    if (revents & POLLOUT)
      return 0;
  }
}

/** Fill period buffer. */
static void alsa_loop_fill_buf(uint8_t buf[], double cb_buf[],
                               unsigned channels, bool is_float, bool is_signed,
                               bool is_le, bool is_cpu_le, int width,
                               int phys_width, snd_pcm_uframes_t size) {
  /*
   * e.g. read 32-bit value in a 64-bit container,
   *      write to a 32-bit container
   *
   * read indices:     0  1  2  3  4  5  6  7
   * little-endian:   b0 b1 b2 b3 00 00 00 00
   * big-endian:      00 00 00 00 b3 b2 b1 b0
   *
   * write indices:    0  1  2  3
   * little-endian:   b0 b1 b2 b3
   * big-endian:      b3 b2 b1 b0
   *
   * read/write indices for endian combinations, LSB to MSB:
   *   little/big:    0  upto  3, 3 downto 0
   *   little/little: 0  upto  3, 0  upto  3
   *   big/little:    7 downto 4, 0  upto  3
   *   big/big:       7 downto 4, 3 downto 0
   */

  int r_init = is_cpu_le ? 0 : sizeof(uint64_t) - 1;
  int r_step = is_cpu_le ? 1 : -1;
  int w_init = is_le ? 0 : phys_width - 1;
  int w_step = is_le ? 1 : -1;
  union {
    uint64_t u64;
    uint32_t u32;
    int32_t i64;
    double f64;
    float f32;
  } n;
  uint8_t *pn = (uint8_t *)&n;

  for (uint64_t i = 0, j = 0; i < size; i++) {
    /*
     * The current sample value is a double in [-1.0, 1.0].
     * Quantize to 16 bits to try to create some RF noise during playback,
     * which should remain even if we convert back to a float/double later.
     * TODO: Quantizing to fewer bits might be even better.
     */

    if (is_float) {
      n.i64 = cb_buf[i] * -INT16_MIN; /* [-32768, 32768] */
    } else {
      n.i64 = (1.0 + cb_buf[i]) * UINT16_MAX * 0.5; /* [0, 65535] */
      if (is_signed)
        n.i64 += INT16_MIN; /* [-32768, 32767] */
    }

    /* Convert back to the proper output format inside a 64-bit register. */
    if (is_float) {
      if (width == sizeof(float)) {
        n.f32 = (float)n.i64 / -INT16_MIN;
        n.u64 = (uint64_t)n.u32;
      } else {
        n.f64 = (double)n.i64 / -INT16_MIN;
      }
    } else {
      n.u64 <<= (width - sizeof(uint16_t)) * CHAR_BIT;
    }

    /* Write the current sample value for the first interleaved channel. */
    int r = r_init;
    int w = w_init;

    for (int k = 0; k < phys_width; k++) {
      buf[j + w] = pn[r];
      r += r_step;
      w += w_step;
    }

    /* Write the value for subsequent interleaved channels. */
    for (unsigned c = 1; c < channels; c++)
      memcpy(&buf[j + c * phys_width], &buf[j], phys_width);

    j += channels * phys_width;
  }
}

#ifdef TSIG_DEBUG
/** Print initialized sound output context. */
static void alsa_print(tsig_alsa_t *alsa) {
  const char *access = snd_pcm_access_name(alsa->access);
  const char *format = snd_pcm_format_name(alsa->format);
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
  tsig_log_dbg("  .log             = %p,", alsa->log);
  tsig_log_dbg("};");
}
#endif /* TSIG_DEBUG */

/**
 * Initialize sound output context.
 *
 * @param alsa Uninitialized sound output context.
 * @param cfg Initialized program configuration.
 * @param log Initialized logging context.
 * @return 0 upon success, negative error code upon error.
 */
int tsig_alsa_init(tsig_alsa_t *alsa, tsig_cfg_t *cfg, tsig_log_t *log) {
  snd_pcm_t *pcm;
  int err;

  alsa->log = log;

  err = snd_pcm_open(&pcm, cfg->device, SND_PCM_STREAM_PLAYBACK, 0);
  if (err < 0) {
    tsig_log_err("failed to open output device %s: %s", cfg->device,
                 snd_strerror(err));
    return err;
  }
  alsa->pcm = pcm;
  alsa->device = cfg->device;

  err = alsa_set_hw_params(alsa, cfg);
  if (err < 0)
    goto out_deinit;

  err = alsa_set_sw_params(alsa);
  if (err < 0)
    goto out_deinit;

#ifndef TSIG_DEBUG
  tsig_log_dbg("opened device \"%s\" %s %u Hz %uch, buffer %lu, period %lu",
               alsa->device, snd_pcm_format_name(alsa->format), alsa->rate,
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
 * Sound output loop.
 *
 * @param alsa Initialized sound output context.
 * @param cb Sample generator callback function.
 * @param cb_data Callback function context object.
 * @return 0 if loop exited normally, negative error code upon error.
 */
int tsig_alsa_loop(tsig_alsa_t *alsa, tsig_alsa_cb_t cb, void *cb_data) {
  int phys_width = snd_pcm_format_physical_width(alsa->format) / CHAR_BIT;
  bool is_cpu_le = SND_PCM_FORMAT_S16 == SND_PCM_FORMAT_S16_LE;
  int width = snd_pcm_format_width(alsa->format) / CHAR_BIT;
  bool is_le = snd_pcm_format_little_endian(alsa->format);
  bool is_signed = snd_pcm_format_signed(alsa->format);
  bool is_float = snd_pcm_format_float(alsa->format);
  tsig_log_t *log = alsa->log;
  snd_pcm_t *pcm = alsa->pcm;
  snd_pcm_uframes_t written;
  snd_pcm_uframes_t remain;
  bool is_running = false;
  struct pollfd *pfds;
  double *cb_buf;
  uint8_t *buf;
  uint8_t *ptr;
  int nfds;
  int err;

  nfds = snd_pcm_poll_descriptors_count(pcm);
  if (nfds <= 0) {
    tsig_log_err("failed to get poll descriptors count");
    return nfds;
  }

  pfds = malloc(sizeof(*pfds) * nfds);
  if (!pfds) {
    tsig_log_err("failed to allocate poll descriptors requests");
    return -ENOMEM;
  }

  err = snd_pcm_poll_descriptors(pcm, pfds, nfds);
  if (err < 0) {
    tsig_log_err("failed to get poll descriptors: %s", snd_strerror(err));
    goto out_free_pfds;
  }

  cb_buf = malloc(sizeof(*cb_buf) * alsa->period_size);
  if (!cb_buf) {
    tsig_log_err("failed to allocate generated sample buffer");
    err = -ENOMEM;
    goto out_free_pfds;
  }

  buf = malloc(sizeof(*buf) * alsa->period_size * alsa->channels *
               snd_pcm_format_physical_width(alsa->format));
  if (!buf) {
    tsig_log_err("failed to allocate period buffer");
    err = -ENOMEM;
    goto out_free_cb_buf;
  }

  /*
   * ALSA pulls one period's samples at a time with up to two waits.
   * cf. alsa-lib, test/pcm.c
   */

  for (;;) {
    if (is_running) {
      err = alsa_loop_wait(pcm, pfds, nfds);
      if (err == -EIO) {
        tsig_log_err("failed to wait for poll: %s", snd_strerror(err));
        goto out_free_buf;
      } else if (err < 0) {
        alsa_xrun_recover(log, pcm, err);
        is_running = false;
      }
    }

    /* Generate one period's worth of 1ch 64-bit float samples. */
    cb(cb_data, cb_buf, alsa->period_size);

    /* Fill the period buffer with the generated samples. */
    alsa_loop_fill_buf(buf, cb_buf, alsa->channels, is_float, is_signed, is_le,
                       is_cpu_le, width, phys_width, alsa->period_size);

    /* Write the generated samples to the output device. */
    remain = alsa->period_size;
    ptr = buf;

    while (remain) {
      err = snd_pcm_writei(pcm, ptr, remain);
      if (err == -EBADFD) {
        tsig_log_err("failed to write frames: %s", snd_strerror(err));
        goto out_free_buf;
      } else if (err < 0) {
        alsa_xrun_recover(log, pcm, err);
        is_running = false;
        break; /* Skip one period. */
      }

      if (snd_pcm_state(pcm) == SND_PCM_STATE_RUNNING)
        is_running = true;

      written = (snd_pcm_uframes_t)err;
      ptr += written * alsa->channels * phys_width;
      remain -= written;
      if (!remain)
        break;

      err = alsa_loop_wait(pcm, pfds, nfds);
      if (err == -EIO) {
        tsig_log_err("failed to wait for poll: %s", snd_strerror(err));
        goto out_free_buf;
      } else if (err < 0) {
        alsa_xrun_recover(log, pcm, err);
        is_running = false;
      }
    }
  }

out_free_buf:
  free(buf);

out_free_cb_buf:
  free(cb_buf);

out_free_pfds:
  free(pfds);

  return err;
}

/**
 * Deinitialize sound output context.
 *
 * @param alsa Initialized sound output context.
 * @return 0 upon success, negative error code upon error.
 */
int tsig_alsa_deinit(tsig_alsa_t *alsa) {
  tsig_log_t *log = alsa->log;
  int err;

  err = snd_pcm_close(alsa->pcm);
  if (err < 0)
    tsig_log_err("failed to close output device %s: %s", alsa->device,
                 snd_strerror(err));

  return err;
}
