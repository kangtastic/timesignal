// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * pulse.c: PulseAudio output facilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "pulse.h"

#include "audio.h"
#include "cfg.h"
#include "defaults.h"
#include "log.h"
#include "mapping.h"

#include <stdint.h>

#include <pulse/pulseaudio.h>

/** Default buffer time in us. */
static const uint64_t pulse_buffer_time = 200000;

/** Default period time in us. */
static const uint64_t pulse_period_time = 100000;

/** Time conversions. */
static const uint64_t pulse_usecs_sec = 1000000;

/** Sample format map. */
static const tsig_mapping_nn_t pulse_format_map[] = {
    {TSIG_AUDIO_FORMAT_S16, PA_SAMPLE_S16NE},
    {TSIG_AUDIO_FORMAT_S16_LE, PA_SAMPLE_S16LE},
    {TSIG_AUDIO_FORMAT_S16_BE, PA_SAMPLE_S16BE},
    {TSIG_AUDIO_FORMAT_S24, PA_SAMPLE_S24_32NE},
    {TSIG_AUDIO_FORMAT_S24_LE, PA_SAMPLE_S24_32LE},
    {TSIG_AUDIO_FORMAT_S24_BE, PA_SAMPLE_S24_32BE},
    {TSIG_AUDIO_FORMAT_S32, PA_SAMPLE_S32NE},
    {TSIG_AUDIO_FORMAT_S32_LE, PA_SAMPLE_S32LE},
    {TSIG_AUDIO_FORMAT_S32_BE, PA_SAMPLE_S32BE},
    /* NOTE: Unsigned formats are not supported. */
    {TSIG_AUDIO_FORMAT_FLOAT, PA_SAMPLE_FLOAT32NE},
    {TSIG_AUDIO_FORMAT_FLOAT_LE, PA_SAMPLE_FLOAT32LE},
    {TSIG_AUDIO_FORMAT_FLOAT_BE, PA_SAMPLE_FLOAT32BE},
    /* NOTE: 64-bit float formats are not supported. */
    {0, 0},
};

/** Sample format lookup. */
pa_sample_format_t pulse_format(const tsig_audio_format_t format) {
  pa_sample_format_t value =
      tsig_mapping_nn_match_key(pulse_format_map, format);
  return value < 0 ? PA_SAMPLE_INVALID : value;
}

/** PulseAudio context state change callback. */
static void pulse_context_state_cb(pa_context *ctx, void *data) {
  tsig_pulse_t *pulse = data;
  pulse->state = pa_context_get_state(ctx);
}

/** PulseAudio stream write callback. */
static void pulse_stream_write_cb(pa_stream *stream, size_t length,
                                  void *data) {
  /* Calculate the number of samples PulseAudio requested. */
  tsig_pulse_t *pulse = data;
  size_t size = length / pulse->stride;

  /* Generate the requisite number of 1ch 64-bit float samples. */
  pulse->cb(pulse->cb_data, pulse->cb_buf, size);

  /* Fill the output buffer with the generated samples. */
  tsig_audio_fill_buffer(pulse->audio_format, pulse->channels, size, pulse->buf,
                         pulse->cb_buf);

  /* Write the output buffer to the PulseAudio stream. */
  pa_stream_write(stream, pulse->buf, length, NULL, 0, PA_SEEK_RELATIVE);
}

#ifdef TSIG_DEBUG
static void pulse_print(tsig_pulse_t *pulse) {
  const char *audio_format = tsig_audio_format_name(pulse->audio_format);
  const char *format = pa_sample_format_to_string(pulse->format);
  tsig_log_t *log = pulse->log;
  tsig_log_dbg("tsig_pulse_t %p = {", pulse);
  tsig_log_dbg("  .loop         = %p,", pulse->loop);
  tsig_log_dbg("  .ctx          = %p,", pulse->ctx);
  tsig_log_dbg("  .state        = %d,", pulse->state);
  tsig_log_dbg("  .format       = %s,", format);
  tsig_log_dbg("  .rate         = %u,", pulse->rate);
  tsig_log_dbg("  .channels     = %u,", pulse->channels);
  tsig_log_dbg("  .cb           = %p,", pulse->cb);
  tsig_log_dbg("  .cb_data      = %p,", pulse->cb_data);
  tsig_log_dbg("  .cb_buf       = %p,", pulse->cb_buf);
  tsig_log_dbg("  .buf          = %p,", pulse->buf);
  tsig_log_dbg("  .stride       = %u,", pulse->stride);
  tsig_log_dbg("  .size         = %u,", pulse->size);
  tsig_log_dbg("  .audio_format = %s,", audio_format);
  tsig_log_dbg("  .log          = %p,", log);
  tsig_log_dbg("};");
}
#endif /* TSIG_DEBUG */

/**
 * Initialize PulseAudio output context.
 *
 * @param pulse Uninitialized PulseAudio output context.
 * @param cfg Initialized program configuration.
 * @param log Initialized logging context.
 * @return 0 on success, or a negative error code upon failure.
 */
int tsig_pulse_init(tsig_pulse_t *pulse, tsig_cfg_t *cfg, tsig_log_t *log) {
  uint32_t buffer_size = pulse_buffer_time * cfg->rate / pulse_usecs_sec;
  pa_sample_format_t format = pulse_format(cfg->format);
  uint16_t channels = cfg->channels;
  uint32_t rate = cfg->rate;
  pa_buffer_attr attr;
  pa_sample_spec spec;
  pa_stream *stream;
  int err = -1;

  *pulse = (tsig_pulse_t){
      .log = log,
  };

  /*
   * As with PipeWire, creating a PulseAudio stream with invalid
   * parameters usually seems to work. PulseAudio provides some
   * validation facilities, but we might as well do it ourselves.
   */

  if (format == PA_SAMPLE_INVALID) {
    format = PA_SAMPLE_S16NE;
    tsig_log_note("failed to set format %s, fallback to %s",
                  tsig_audio_format_name(cfg->format),
                  tsig_audio_format_name(TSIG_AUDIO_FORMAT_S16));
  }

  if (rate > PA_RATE_MAX) {
    rate = PA_RATE_MAX;
    tsig_log_note("failed to set rate near %u, fallback to %u", cfg->rate,
                  rate);
  }

  if (channels > PA_CHANNELS_MAX) {
    channels = PA_CHANNELS_MAX;
    tsig_log_note("failed to set channels %hu, fallback to %hu", cfg->channels,
                  channels);
  }

  pulse->loop = pa_mainloop_new();
  if (!pulse->loop) {
    tsig_log_err("failed to create PulseAudio main loop");
    return err;
  }

  pulse->ctx =
      pa_context_new(pa_mainloop_get_api(pulse->loop), TSIG_DEFAULTS_NAME);
  if (!pulse->ctx) {
    tsig_log_err("failed to create PulseAudio context");
    goto out_deinit;
  }

  pa_context_set_state_callback(pulse->ctx, pulse_context_state_cb, pulse);
  err = pa_context_connect(pulse->ctx, NULL, 0, NULL);
  if (err < 0) {
    tsig_log_err("failed to connect to PulseAudio context");
    goto out_deinit;
  }

  /* Wait until the PulseAudio context is ready. */
  while (pulse->state != PA_CONTEXT_READY) {
    err = pa_mainloop_iterate(pulse->loop, 1, NULL);
    if (err < 0) {
      tsig_log_err("failed iterating PulseAudio main loop");
      goto out_deinit;
    }

    if (pulse->state == PA_CONTEXT_FAILED ||
        pulse->state == PA_CONTEXT_TERMINATED) {
      tsig_log_err("failed to make PulseAudio context ready");
      goto out_deinit;
    }
  }

  spec = (pa_sample_spec){.format = format, .rate = rate, .channels = channels};
  stream = pa_stream_new(pulse->ctx, TSIG_DEFAULTS_NAME "-pulse", &spec, NULL);
  if (!stream) {
    tsig_log_err("failed to create PulseAudio stream");
    goto out_deinit;
  }
  pa_stream_set_write_callback(stream, pulse_stream_write_cb, pulse);

  attr = (pa_buffer_attr){
      .fragsize = (uint32_t)-1,
      .maxlength = pa_usec_to_bytes(pulse_buffer_time, &spec),
      .minreq = pa_usec_to_bytes(pulse_period_time, &spec),
      .prebuf = (uint32_t)-1,
      .tlength = pa_usec_to_bytes(pulse_buffer_time, &spec),
  };
  err = pa_stream_connect_playback(
      stream, NULL, &attr,
      PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE, NULL, NULL);
  if (err < 0) {
    tsig_log_err("failed to connect to PulseAudio stream");
    goto out_deinit;
  }

  /* Sample generator callback is set in tsig_pulse_loop(). */
  pulse->format = format;
  pulse->rate = rate;
  pulse->channels = channels;
  pulse->size = buffer_size;
  pulse->audio_format = tsig_mapping_nn_match_value(pulse_format_map, format);
  pulse->stride = tsig_audio_format_phys_width(pulse->audio_format) * channels;

  /*
   * We don't know how many 1ch 64-bit float samples to generate for a given
   * stream write callback until it actually occurs. Allocate a buffer capable
   * of holding enough generated samples to fill the entire PulseAudio output
   * buffer, which should be about twice as large as we'll ever need.
   */

  pulse->cb_buf = malloc(buffer_size * sizeof(double));
  if (!pulse->cb_buf) {
    tsig_log_err("failed to allocate generated sample buffer");
    err = -ENOMEM;
    goto out_deinit;
  }

  /*
   * Allocate a client-side buffer to hold those samples once converted into the
   * proper output format. Zero-copy writes via pa_stream_begin_write() would be
   * ideal, but in testing underruns resulted from certain stream parameters.
   */

  pulse->buf = malloc(pulse->stride * buffer_size);
  if (!pulse->buf) {
    tsig_log_err("failed to allocate client-side output buffer");
    err = -ENOMEM;
    goto out_deinit;
  }

#ifndef TSIG_DEBUG
  tsig_log_dbg("started PulseAudio stream %s %u Hz %uch, buffer %lu",
               pa_sample_format_to_string(format), rate, channels, buffer_size);
#else
  pulse_print(pulse);
#endif /* TSIG_DEBUG */

  return 0;

out_deinit:
  tsig_pulse_deinit(pulse);

  return err;
}

/**
 * PulseAudio output loop.
 *
 * @param pulse Initialized PulseAudio output context.
 * @param cb Sample generator callback function.
 * @param cb_data Callback function context object.
 * @return 0 if loop exited normally, negative error code upon error.
 */
int tsig_pulse_loop(tsig_pulse_t *pulse, tsig_audio_cb_t cb, void *cb_data) {
  pulse->cb = cb;
  pulse->cb_data = cb_data;

  return pa_mainloop_run(pulse->loop, NULL);
}

/**
 * Deinitialize PulseAudio output context.
 *
 * @param pulse Initialized PulseAudio output context.
 * @return 0 upon success, negative error code upon error.
 */
int tsig_pulse_deinit(tsig_pulse_t *pulse) {
  if (pulse->ctx) {
    pa_context_disconnect(pulse->ctx);
    pa_context_unref(pulse->ctx);
  }

  if (pulse->loop)
    pa_mainloop_free(pulse->loop);

  free(pulse->cb_buf);
  free(pulse->buf);

  return 0;
}
