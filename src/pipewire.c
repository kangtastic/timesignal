// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * pipewire.c: PipeWire output facilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "pipewire.h"

#include "audio.h"
#include "cfg.h"
#include "defaults.h"
#include "log.h"
#include "mapping.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

#include <dlfcn.h>
#include <unistd.h>

#include <signal.h>
#include <stdint.h>

/** PipeWire library shared object name. */
static const char *pipewire_lib_soname = "libpipewire-0.3.so.0";

/** PipeWire library handle. */
static void *pipewire_lib;

/** Pointers to PipeWire library functions. */
/* clang-format off */
static void (*pipewire_pw_init)(int *argc, char ***argv);
static void (*pipewire_pw_main_loop_destroy)(struct pw_main_loop *loop);
static struct pw_loop *(*pipewire_pw_main_loop_get_loop)(struct pw_main_loop *loop);
static struct pw_main_loop *(*pipewire_pw_main_loop_new)(const struct spa_dict *props);
static int (*pipewire_pw_main_loop_quit)(struct pw_main_loop *loop);
static int (*pipewire_pw_main_loop_run)(struct pw_main_loop *loop);
static struct pw_properties *(*pipewire_pw_properties_new)(const char *key, ...);
static int (*pipewire_pw_properties_setf)(struct pw_properties *properties, const char *key, const char *format, ...);
static int (*pipewire_pw_stream_connect)(struct pw_stream *stream, enum spa_direction direction, uint32_t target_id, enum pw_stream_flags flags, const struct spa_pod **params, uint32_t n_params);
static struct pw_buffer *(*pipewire_pw_stream_dequeue_buffer)(struct pw_stream *stream);
static void (*pipewire_pw_stream_destroy)(struct pw_stream *stream);
static struct pw_stream *(*pipewire_pw_stream_new_simple)(struct pw_loop *loop, const char *name, struct pw_properties *props, const struct pw_stream_events *events, void *data);
static int (*pipewire_pw_stream_queue_buffer)(struct pw_stream *stream, struct pw_buffer *buffer);
/* clang-format on */

/** Default buffer time in us. */
static const uint64_t pipewire_buffer_time = 200000;

/** Time conversions. */
static const uint64_t pipewire_usecs_sec = 1000000;

/** Sample format map. */
static const tsig_mapping_nn_t pipewire_format_map[] = {
    {TSIG_AUDIO_FORMAT_S16, SPA_AUDIO_FORMAT_S16},
    {TSIG_AUDIO_FORMAT_S16_LE, SPA_AUDIO_FORMAT_S16_LE},
    {TSIG_AUDIO_FORMAT_S16_BE, SPA_AUDIO_FORMAT_S16_BE},
    {TSIG_AUDIO_FORMAT_S24, SPA_AUDIO_FORMAT_S24_32},
    {TSIG_AUDIO_FORMAT_S24_LE, SPA_AUDIO_FORMAT_S24_32_LE},
    {TSIG_AUDIO_FORMAT_S24_BE, SPA_AUDIO_FORMAT_S24_32_BE},
    {TSIG_AUDIO_FORMAT_S32, SPA_AUDIO_FORMAT_S32},
    {TSIG_AUDIO_FORMAT_S32_LE, SPA_AUDIO_FORMAT_S32_LE},
    {TSIG_AUDIO_FORMAT_S32_BE, SPA_AUDIO_FORMAT_S32_BE},
    /* NOTE: Unsigned formats don't seem to work. */
    {TSIG_AUDIO_FORMAT_FLOAT, SPA_AUDIO_FORMAT_F32},
    {TSIG_AUDIO_FORMAT_FLOAT_LE, SPA_AUDIO_FORMAT_F32_LE},
    {TSIG_AUDIO_FORMAT_FLOAT_BE, SPA_AUDIO_FORMAT_F32_BE},
    {TSIG_AUDIO_FORMAT_FLOAT64, SPA_AUDIO_FORMAT_F64},
    {TSIG_AUDIO_FORMAT_FLOAT64_LE, SPA_AUDIO_FORMAT_F64_LE},
    {TSIG_AUDIO_FORMAT_FLOAT64_BE, SPA_AUDIO_FORMAT_F64_BE},
    {0, 0},
};

/** Sample format names. */
static const tsig_mapping_t pipewire_formats[] = {
    {"S16_LE", SPA_AUDIO_FORMAT_S16_LE},
    {"S16_BE", SPA_AUDIO_FORMAT_S16_BE},
    {"S24_32_LE", SPA_AUDIO_FORMAT_S24_32_LE},
    {"S24_32_BE", SPA_AUDIO_FORMAT_S24_32_BE},
    {"S32_LE", SPA_AUDIO_FORMAT_S32_LE},
    {"S32_BE", SPA_AUDIO_FORMAT_S32_BE},
    /* NOTE: We need no names for unsigned formats. */
    {"F32_LE", SPA_AUDIO_FORMAT_F32_LE},
    {"F32_BE", SPA_AUDIO_FORMAT_F32_BE},
    {"F64_LE", SPA_AUDIO_FORMAT_F64_LE},
    {"F64_BE", SPA_AUDIO_FORMAT_F64_BE},
    {NULL, 0},
};

/** Sample format lookup. */
static enum spa_audio_format pipewire_format(const tsig_audio_format_t format) {
  enum spa_audio_format value =
      tsig_mapping_nn_match_key(pipewire_format_map, format);
  return value < 0 ? SPA_AUDIO_FORMAT_UNKNOWN : value;
}

/** Sample format name lookup. */
static const char *pipewire_format_name(const enum spa_audio_format format) {
  return tsig_mapping_match_value(pipewire_formats, format);
}

/** PipeWire signal handler. */
static void pipewire_on_signal(void *data, int signal) {
  tsig_pipewire_t *pipewire = data;
  (void)signal; /* Suppress unused parameter warning. */
  pipewire_pw_main_loop_quit(pipewire->loop);
}

/** PipeWire process event callback. */
static void pipewire_on_process(void *data) {
  tsig_pipewire_t *pipewire = data;
  tsig_log_t *log = pipewire->log;
  struct spa_buffer *spa_buf;
  struct pw_buffer *pw_buf;
  uint64_t size;
  uint8_t *buf;

  pw_buf = pipewire_pw_stream_dequeue_buffer(pipewire->stream);
  if (!pw_buf) {
    tsig_log_warn("failed to dequeue buffer during process event");
    return;
  }

  spa_buf = pw_buf->buffer;
  buf = spa_buf->datas[0].data;
  if (!buf) {
    tsig_log_warn("failed to locate output buffer during process event");
    return;
  }

  /*
   * We don't know the number of samples PipeWire wants ahead of time,
   * and old versions can't even tell us in struct pw_buffer::requested.
   * (PipeWire src/pipewire/stream.h wrongly claims it was added in 0.3.49.)
   */

  size = spa_buf->datas[0].maxsize / pipewire->stride;

#if PW_CHECK_VERSION(0, 3, 50)
  if (size > pw_buf->requested)
    size = pw_buf->requested;
#endif

  /* We must not generate more samples than can fit in pipewire->cb_buf. */
  if (size > pipewire->size)
    size = pipewire->size;

  /* Generate the requisite number of 1ch 64-bit float samples. */
  pipewire->cb(pipewire->cb_data, pipewire->cb_buf, size);

  /* Fill the output buffer with the generated samples. */
  tsig_audio_fill_buffer(pipewire->audio_format, pipewire->channels, size, buf,
                         pipewire->cb_buf);

  spa_buf->datas[0].chunk->offset = 0;
  spa_buf->datas[0].chunk->stride = pipewire->stride;
  spa_buf->datas[0].chunk->size = size * pipewire->stride;

  pipewire_pw_stream_queue_buffer(pipewire->stream, pw_buf);
}

/** Stream events. */
static const struct pw_stream_events pipewire_stream_events = {
    .version = PW_VERSION_STREAM_EVENTS,
    .process = pipewire_on_process,
};

#ifdef TSIG_DEBUG
static void pipewire_print(tsig_pipewire_t *pipewire) {
  const char *audio_format = tsig_audio_format_name(pipewire->audio_format);
  const char *format = pipewire_format_name(pipewire->format);
  tsig_log_t *log = pipewire->log;
  tsig_log_dbg("tsig_pipewire_t %p = {", pipewire);
  tsig_log_dbg("  .loop         = %p,", pipewire->loop);
  tsig_log_dbg("  .stream       = %p,", pipewire->stream);
  tsig_log_dbg("  .format       = %s,", format);
  tsig_log_dbg("  .rate         = %" PRIu32 ",", pipewire->rate);
  tsig_log_dbg("  .channels     = %" PRIu16 ",", pipewire->channels);
  tsig_log_dbg("  .cb           = %p,", pipewire->cb);
  tsig_log_dbg("  .cb_data      = %p,", pipewire->cb_data);
  tsig_log_dbg("  .cb_buf       = %p,", pipewire->cb_buf);
  tsig_log_dbg("  .stride       = %" PRIu32 ",", pipewire->stride);
  tsig_log_dbg("  .size         = %" PRIu32 ",", pipewire->size);
  tsig_log_dbg("  .audio_format = %s,", audio_format);
  tsig_log_dbg("  .timeout      = %u,", pipewire->timeout);
  tsig_log_dbg("  .log          = %p,", log);
  tsig_log_dbg("};");
}
#endif /* TSIG_DEBUG */

/**
 * Initialize PipeWire output.
 *
 * @param log Initialized logging context.
 * @return 0 upon success, negative error code upon error.
 */
int tsig_pipewire_lib_init(tsig_log_t *log) {
  pipewire_lib = dlopen(pipewire_lib_soname, RTLD_LAZY);
  if (!pipewire_lib) {
    tsig_log_err("failed to load PipeWire library: %s", dlerror());
    return -EINVAL;
  }

#define pipewire_dlsym_assign(f)                                          \
  do {                                                                    \
    *(void **)(&pipewire_##f) = dlsym(pipewire_lib, #f);                  \
    if (!pipewire_##f) {                                                  \
      tsig_log_err("failed to load PipeWire library function %s: %s", #f, \
                   dlerror());                                            \
      return -EINVAL;                                                     \
    }                                                                     \
  } while (0)

  pipewire_dlsym_assign(pw_init);
  pipewire_dlsym_assign(pw_main_loop_destroy);
  pipewire_dlsym_assign(pw_main_loop_get_loop);
  pipewire_dlsym_assign(pw_main_loop_new);
  pipewire_dlsym_assign(pw_main_loop_quit);
  pipewire_dlsym_assign(pw_main_loop_run);
  pipewire_dlsym_assign(pw_properties_new);
  pipewire_dlsym_assign(pw_properties_setf);
  pipewire_dlsym_assign(pw_stream_connect);
  pipewire_dlsym_assign(pw_stream_dequeue_buffer);
  pipewire_dlsym_assign(pw_stream_destroy);
  pipewire_dlsym_assign(pw_stream_new_simple);
  pipewire_dlsym_assign(pw_stream_queue_buffer);

#undef pipewire_dlsym_assign

  return 0;
}

/**
 * Initialize PipeWire output context.
 *
 * @param pipewire Uninitialized PipeWire output context.
 * @param cfg Initialized program configuration.
 * @param log Initialized logging context.
 * @return 0 on success, or a negative error code upon failure.
 */
int tsig_pipewire_init(tsig_pipewire_t *pipewire, tsig_cfg_t *cfg,
                       tsig_log_t *log) {
  uint32_t buffer_size = pipewire_buffer_time * cfg->rate / pipewire_usecs_sec;
  enum spa_audio_format format = pipewire_format(cfg->format);
  bool is_le = tsig_audio_is_cpu_le();
  uint16_t channels = cfg->channels;
  const struct spa_pod *params[1];
  struct spa_pod_builder builder;
  struct pw_properties *props;
  struct pw_loop *loop;
  uint8_t buffer[1024];
  int err = -1;

  *pipewire = (tsig_pipewire_t){
      .timeout = cfg->timeout,
      .log = log,
  };

  /*
   * Annoyingly, creating a PipeWire stream with invalid parameters always
   * appears successful at first; none of the API functions involved returns
   * NULL or an error code, and the only indication something went wrong is
   * audio playback silently failing later on. Work around this by validating
   * parameters ourselves and accepting only those that seem likely to work.
   */

  if ((is_le && cfg->format == TSIG_AUDIO_FORMAT_FLOAT64_BE) ||
      (!is_le && cfg->format == TSIG_AUDIO_FORMAT_FLOAT64_LE) ||
      format == SPA_AUDIO_FORMAT_UNKNOWN) {
    format = SPA_AUDIO_FORMAT_S16;
    tsig_log_note("failed to set format %s, fallback to %s",
                  tsig_audio_format_name(cfg->format),
                  tsig_audio_format_name(TSIG_AUDIO_FORMAT_S16));
  }

  if (channels > SPA_AUDIO_MAX_CHANNELS) {
    channels = SPA_AUDIO_MAX_CHANNELS;
    tsig_log_note("failed to set channels %" PRIu16 ", fallback to %" PRIu16,
                  cfg->channels, channels);
  }

  pipewire_pw_init(NULL, NULL);

  pipewire->loop = pipewire_pw_main_loop_new(NULL);
  if (!pipewire->loop) {
    tsig_log_err("failed to create PipeWire main loop");
    return err;
  }

  /* clang-format off */
  props = pipewire_pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Audio",
      PW_KEY_MEDIA_CATEGORY, "Playback",
      PW_KEY_MEDIA_ROLE, "Music",
      NULL
  );
  /* clang-format on */
  pipewire_pw_properties_setf(props, PW_KEY_NODE_LATENCY,
                              "%" PRIu32 "/%" PRIu32, buffer_size, cfg->rate);
  loop = pipewire_pw_main_loop_get_loop(pipewire->loop);

  pipewire->stream =
      pipewire_pw_stream_new_simple(loop, TSIG_DEFAULTS_NAME "-pipewire", props,
                                    &pipewire_stream_events, (void *)pipewire);
  if (!pipewire->stream) {
    tsig_log_err("failed to create PipeWire stream");
    goto out_deinit;
  }

  builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
  params[0] = spa_format_audio_raw_build(
      &builder, SPA_PARAM_EnumFormat,
      &SPA_AUDIO_INFO_RAW_INIT(.format = format, .channels = channels,
                               .rate = cfg->rate));

  /* NOTE: We don't pass PW_STREAM_FLAG_RT_PROCESS as we don't need it. */
  err = pipewire_pw_stream_connect(
      pipewire->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
      PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS, params, 1);
  if (err < 0) {
    tsig_log_err("failed to connect to PipeWire stream");
    goto out_deinit;
  }

  /* Sample generator callback is set in tsig_pipewire_loop(). */
  pipewire->format = format;
  pipewire->rate = cfg->rate;
  pipewire->channels = channels;
  pipewire->size = buffer_size;
  pipewire->audio_format =
      tsig_mapping_nn_match_value(pipewire_format_map, format);
  pipewire->stride =
      tsig_audio_format_phys_width(pipewire->audio_format) * channels;

  /*
   * We don't know how many 1ch 64-bit float samples to generate for a given
   * process event until it actually occurs. Allocate a buffer capable of
   * holding enough generated samples to fill the entire PipeWire output
   * buffer, which should be at least twice as large as we'll ever need.
   */

  pipewire->cb_buf = malloc(buffer_size * sizeof(double));
  if (!pipewire->cb_buf) {
    tsig_log_err("failed to allocate generated sample buffer");
    err = -ENOMEM;
    goto out_deinit;
  }

#ifndef TSIG_DEBUG
  tsig_log_dbg(
      "started PipeWire stream %s"
      " %" PRIu32 " Hz %" PRIu16 "ch, buffer %" PRIu32,
      pipewire_format_name(format), cfg->rate, channels, buffer_size);
#else
  pipewire_print(pipewire);
#endif /* TSIG_DEBUG */

  return 0;

out_deinit:
  tsig_pipewire_deinit(pipewire);

  return err;
}

/**
 * PipeWire output loop.
 *
 * @param pipewire Initialized PipeWire output context.
 * @param cb Sample generator callback function.
 * @param cb_data Callback function context object.
 * @return 0 if loop exited normally, negative error code upon error.
 */
int tsig_pipewire_loop(tsig_pipewire_t *pipewire, tsig_audio_cb_t cb,
                       void *cb_data) {
  struct pw_loop *loop = pipewire_pw_main_loop_get_loop(pipewire->loop);
  int err;

  /* Install PipeWire signal handler. */
  pw_loop_add_signal(loop, SIGINT, pipewire_on_signal, pipewire);
  pw_loop_add_signal(loop, SIGTERM, pipewire_on_signal, pipewire);
  pw_loop_add_signal(loop, SIGALRM, pipewire_on_signal, pipewire);

  pipewire->cb = cb;
  pipewire->cb_data = cb_data;

  alarm(pipewire->timeout);
  err = pipewire_pw_main_loop_run(pipewire->loop);
  alarm(0);

  return err;
}

/**
 * Deinitialize PipeWire output context.
 *
 * @param pipewire Initialized PipeWire output context.
 * @return 0 upon success, negative error code upon error.
 */
int tsig_pipewire_deinit(tsig_pipewire_t *pipewire) {
  if (pipewire->stream)
    pipewire_pw_stream_destroy(pipewire->stream);

  if (pipewire->loop)
    pipewire_pw_main_loop_destroy(pipewire->loop);

  free(pipewire->cb_buf);

  return 0;
}

/**
 * Deinitialize PipeWire output.
 *
 * @param log Initialized logging context.
 * @return 0 upon success, negative error code upon error.
 */
int tsig_pipewire_lib_deinit(tsig_log_t *log) {
  if (!dlclose(pipewire_lib))
    return 0;

  tsig_log_err("failed to unload PipeWire library: %s", dlerror());

  return -EINVAL;
}
