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

#include <dlfcn.h>
#include <inttypes.h>
#include <stdint.h>
#include <signal.h>

#include <unistd.h>

#include <pulse/pulseaudio.h>

/** PulseAudio library shared object name. */
static const char *pulse_lib_soname = "libpulse.so.0";

/** PulseAudio library handle. */
static void *pulse_lib;

/** Pointers to PulseAudio library functions. */
/* clang-format off */
static int (*pulse_pa_context_connect)(pa_context *c, const char *server, pa_context_flags_t flags, const pa_spawn_api *api);
static void (*pulse_pa_context_disconnect)(pa_context *c);
static pa_context_state_t (*pulse_pa_context_get_state)(const pa_context *c);
static pa_context *(*pulse_pa_context_new)(pa_mainloop_api *mainloop, const char *name);
static void (*pulse_pa_context_set_state_callback)(pa_context *c, pa_context_notify_cb_t cb, void *userdata);
static void (*pulse_pa_context_unref)(pa_context *c);
static void (*pulse_pa_mainloop_free)(pa_mainloop *m);
static pa_mainloop_api *(*pulse_pa_mainloop_get_api)(pa_mainloop *m);
static int (*pulse_pa_mainloop_iterate)(pa_mainloop *m, int block, int *retval);
static pa_mainloop *(*pulse_pa_mainloop_new)(void);
static void (*pulse_pa_mainloop_quit)(pa_mainloop *m, int retval);
static int (*pulse_pa_mainloop_run)(pa_mainloop *m, int *retval);
static const char *(*pulse_pa_sample_format_to_string)(pa_sample_format_t f);
static void (*pulse_pa_signal_done)(void);
static int (*pulse_pa_signal_init)(pa_mainloop_api *api);
static pa_signal_event *(*pulse_pa_signal_new)(int sig, pa_signal_cb_t callback, void *userdata);
static int (*pulse_pa_stream_connect_playback)(pa_stream *s, const char *dev, const pa_buffer_attr *attr, pa_stream_flags_t flags, const pa_cvolume *volume, pa_stream *sync_stream);
static pa_stream *(*pulse_pa_stream_new)(pa_context *c, const char *name, const pa_sample_spec *ss, const pa_channel_map *map);
static void (*pulse_pa_stream_set_write_callback)(pa_stream *p, pa_stream_request_cb_t cb, void *userdata);
static int (*pulse_pa_stream_write)(pa_stream *p, const void *data, size_t nbytes, pa_free_cb_t free_cb, int64_t offset, pa_seek_mode_t seek);
static size_t (*pulse_pa_usec_to_bytes)(pa_usec_t t, const pa_sample_spec *spec);
/* clang-format on */

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

/** PulseAudio signal handler. */
static void pulse_signal_cb(pa_mainloop_api *api, pa_signal_event *event,
                            int signal, void *data) {
  tsig_pulse_t *pulse = data;
  (void)api;    /* Suppress unused parameter warning. */
  (void)event;  /* Suppress unused parameter warning. */
  (void)signal; /* Suppress unused parameter warning. */
  pulse_pa_mainloop_quit(pulse->loop, 0);
}

/** PulseAudio context state change callback. */
static void pulse_context_state_cb(pa_context *ctx, void *data) {
  tsig_pulse_t *pulse = data;
  pulse->state = pulse_pa_context_get_state(ctx);
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
  pulse_pa_stream_write(stream, pulse->buf, length, NULL, 0, PA_SEEK_RELATIVE);
}

#ifdef TSIG_DEBUG
static void pulse_print(tsig_pulse_t *pulse) {
  const char *audio_format = tsig_audio_format_name(pulse->audio_format);
  const char *format = pulse_pa_sample_format_to_string(pulse->format);
  tsig_log_t *log = pulse->log;
  tsig_log_dbg("tsig_pulse_t %p = {", pulse);
  tsig_log_dbg("  .loop         = %p,", pulse->loop);
  tsig_log_dbg("  .ctx          = %p,", pulse->ctx);
  tsig_log_dbg("  .state        = %d,", pulse->state);
  tsig_log_dbg("  .format       = %s,", format);
  tsig_log_dbg("  .rate         = %" PRIu32 ",", pulse->rate);
  tsig_log_dbg("  .channels     = %" PRIu8 ",", pulse->channels);
  tsig_log_dbg("  .cb           = %p,", pulse->cb);
  tsig_log_dbg("  .cb_data      = %p,", pulse->cb_data);
  tsig_log_dbg("  .cb_buf       = %p,", pulse->cb_buf);
  tsig_log_dbg("  .buf          = %p,", pulse->buf);
  tsig_log_dbg("  .stride       = %" PRIu32 ",", pulse->stride);
  tsig_log_dbg("  .size         = %" PRIu32 ",", pulse->size);
  tsig_log_dbg("  .audio_format = %s,", audio_format);
  tsig_log_dbg("  .timeout      = %u,", pulse->timeout);
  tsig_log_dbg("  .log          = %p,", log);
  tsig_log_dbg("};");
}
#endif /* TSIG_DEBUG */

/**
 * Initialize PulseAudio output.
 *
 * @param log Initialized logging context.
 * @return 0 upon success, negative error code upon error.
 */
int tsig_pulse_lib_init(tsig_log_t *log) {
  pulse_lib = dlopen(pulse_lib_soname, RTLD_LAZY);
  if (!pulse_lib) {
    tsig_log_err("failed to load PulseAudio library: %s", dlerror());
    return -EINVAL;
  }

#define pulse_dlsym_assign(f)                                               \
  do {                                                                      \
    *(void **)(&pulse_##f) = dlsym(pulse_lib, #f);                          \
    if (!pulse_##f) {                                                       \
      tsig_log_err("failed to load PulseAudio library function %s: %s", #f, \
                   dlerror());                                              \
      return -EINVAL;                                                       \
    }                                                                       \
  } while (0)

  pulse_dlsym_assign(pa_context_connect);
  pulse_dlsym_assign(pa_context_disconnect);
  pulse_dlsym_assign(pa_context_get_state);
  pulse_dlsym_assign(pa_context_new);
  pulse_dlsym_assign(pa_context_set_state_callback);
  pulse_dlsym_assign(pa_context_unref);
  pulse_dlsym_assign(pa_mainloop_free);
  pulse_dlsym_assign(pa_mainloop_get_api);
  pulse_dlsym_assign(pa_mainloop_iterate);
  pulse_dlsym_assign(pa_mainloop_new);
  pulse_dlsym_assign(pa_mainloop_quit);
  pulse_dlsym_assign(pa_mainloop_run);
  pulse_dlsym_assign(pa_sample_format_to_string);
  pulse_dlsym_assign(pa_signal_done);
  pulse_dlsym_assign(pa_signal_init);
  pulse_dlsym_assign(pa_signal_new);
  pulse_dlsym_assign(pa_stream_connect_playback);
  pulse_dlsym_assign(pa_stream_new);
  pulse_dlsym_assign(pa_stream_set_write_callback);
  pulse_dlsym_assign(pa_stream_write);
  pulse_dlsym_assign(pa_usec_to_bytes);

#undef pulse_dlsym_assign

  return 0;
}

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
      .timeout = cfg->timeout,
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
    tsig_log_note("failed to set rate near %" PRIu32 ", fallback to %" PRIu32,
                  cfg->rate, rate);
  }

  if (channels > PA_CHANNELS_MAX) {
    channels = PA_CHANNELS_MAX;
    tsig_log_note("failed to set channels %" PRIu16 ", fallback to %" PRIu16,
                  cfg->channels, channels);
  }

  pulse->loop = pulse_pa_mainloop_new();
  if (!pulse->loop) {
    tsig_log_err("failed to create PulseAudio main loop");
    return err;
  }

  pulse->ctx = pulse_pa_context_new(pulse_pa_mainloop_get_api(pulse->loop),
                                    TSIG_DEFAULTS_NAME);
  if (!pulse->ctx) {
    tsig_log_err("failed to create PulseAudio context");
    goto out_deinit;
  }

  pulse_pa_context_set_state_callback(pulse->ctx, pulse_context_state_cb,
                                      pulse);
  err = pulse_pa_context_connect(pulse->ctx, NULL, 0, NULL);
  if (err < 0) {
    tsig_log_err("failed to connect to PulseAudio context");
    goto out_deinit;
  }

  /* Wait until the PulseAudio context is ready. */
  while (pulse->state != PA_CONTEXT_READY) {
    err = pulse_pa_mainloop_iterate(pulse->loop, 1, NULL);
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
  stream =
      pulse_pa_stream_new(pulse->ctx, TSIG_DEFAULTS_NAME "-pulse", &spec, NULL);
  if (!stream) {
    tsig_log_err("failed to create PulseAudio stream");
    goto out_deinit;
  }
  pulse_pa_stream_set_write_callback(stream, pulse_stream_write_cb, pulse);

  attr = (pa_buffer_attr){
      .fragsize = (uint32_t)-1,
      .maxlength = pulse_pa_usec_to_bytes(pulse_buffer_time, &spec),
      .minreq = pulse_pa_usec_to_bytes(pulse_period_time, &spec),
      .prebuf = (uint32_t)-1,
      .tlength = pulse_pa_usec_to_bytes(pulse_buffer_time, &spec),
  };
  err = pulse_pa_stream_connect_playback(
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
  tsig_log_dbg(
      "started PulseAudio stream %s"
      " %" PRIu32 " Hz %" PRIu16 "ch, buffer %" PRIu32,
      pulse_pa_sample_format_to_string(format), rate, channels, buffer_size);
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
  tsig_log_t *log = pulse->log;
  int loop_ret = 0;
  int err;

  /* Install PulseAudio signal handler.*/
  err = pulse_pa_signal_init(pulse_pa_mainloop_get_api(pulse->loop));
  if (err < 0) {
    tsig_log_err("failed to initialize PulseAudio signal subsystem");
    return err;
  }

  pulse_pa_signal_new(SIGINT, pulse_signal_cb, pulse);
  pulse_pa_signal_new(SIGTERM, pulse_signal_cb, pulse);
  pulse_pa_signal_new(SIGALRM, pulse_signal_cb, pulse);

  pulse->cb = cb;
  pulse->cb_data = cb_data;

  alarm(pulse->timeout);
  err = pulse_pa_mainloop_run(pulse->loop, &loop_ret);
  pulse_pa_signal_done();
  alarm(0);

  /* cf. PulseAudio src/pulse/mainloop.c pa_mainloop_run() */
  return err < 0 ? err : loop_ret;
}

/**
 * Deinitialize PulseAudio output context.
 *
 * @param pulse Initialized PulseAudio output context.
 * @return 0 upon success, negative error code upon error.
 */
int tsig_pulse_deinit(tsig_pulse_t *pulse) {
  if (pulse->ctx) {
    pulse_pa_context_disconnect(pulse->ctx);
    pulse_pa_context_unref(pulse->ctx);
  }

  if (pulse->loop)
    pulse_pa_mainloop_free(pulse->loop);

  free(pulse->cb_buf);
  free(pulse->buf);

  return 0;
}

/**
 * Deinitialize PulseAudio output.
 *
 * @param log Initialized logging context.
 * @return 0 upon success, negative error code upon error.
 */
int tsig_pulse_lib_deinit(tsig_log_t *log) {
  if (!dlclose(pulse_lib))
    return 0;

  tsig_log_err("failed to unload PulseAudio library: %s", dlerror());

  return -EINVAL;
}
