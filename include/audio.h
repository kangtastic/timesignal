/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * audio.h: Header for generalized audio facilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Recognized sample formats. */
typedef enum tsig_audio_format {
  TSIG_AUDIO_FORMAT_UNKNOWN = -1,
  TSIG_AUDIO_FORMAT_S16,
  TSIG_AUDIO_FORMAT_S16_LE,
  TSIG_AUDIO_FORMAT_S16_BE,
  TSIG_AUDIO_FORMAT_S24,
  TSIG_AUDIO_FORMAT_S24_LE,
  TSIG_AUDIO_FORMAT_S24_BE,
  TSIG_AUDIO_FORMAT_S32,
  TSIG_AUDIO_FORMAT_S32_LE,
  TSIG_AUDIO_FORMAT_S32_BE,
  TSIG_AUDIO_FORMAT_U16,
  TSIG_AUDIO_FORMAT_U16_LE,
  TSIG_AUDIO_FORMAT_U16_BE,
  TSIG_AUDIO_FORMAT_U24,
  TSIG_AUDIO_FORMAT_U24_LE,
  TSIG_AUDIO_FORMAT_U24_BE,
  TSIG_AUDIO_FORMAT_U32,
  TSIG_AUDIO_FORMAT_U32_LE,
  TSIG_AUDIO_FORMAT_U32_BE,
  TSIG_AUDIO_FORMAT_FLOAT,
  TSIG_AUDIO_FORMAT_FLOAT_LE,
  TSIG_AUDIO_FORMAT_FLOAT_BE,
  TSIG_AUDIO_FORMAT_FLOAT64,
  TSIG_AUDIO_FORMAT_FLOAT64_LE,
  TSIG_AUDIO_FORMAT_FLOAT64_BE,
} tsig_audio_format_t;

/** Recognized sample rates. */
typedef enum tsig_audio_rate {
  TSIG_AUDIO_RATE_UNKNOWN = -1,
  TSIG_AUDIO_RATE_44100 = 44100,
  TSIG_AUDIO_RATE_48000 = 48000,
  TSIG_AUDIO_RATE_88200 = 88200,
  TSIG_AUDIO_RATE_96000 = 96000,
  TSIG_AUDIO_RATE_176400 = 176400,
  TSIG_AUDIO_RATE_192000 = 192000,
  TSIG_AUDIO_RATE_352800 = 352800,
  TSIG_AUDIO_RATE_384000 = 384000,
} tsig_audio_rate_t;

/**
 * Pointer to sample generator callback function.
 *
 * @param cb_data Callback function context object.
 * @param[out] out_cb_buf Buffer to be filled with 1ch 64-bit float samples.
 * @param size Count of samples to be generated.
 */
typedef void (*tsig_audio_cb_t)(void *cb_data, double out_cb_buf[],
                                uint32_t size);

tsig_audio_format_t tsig_audio_format(const char *);
const char *tsig_audio_format_name(tsig_audio_format_t);
size_t tsig_audio_format_phys_width(tsig_audio_format_t);
tsig_audio_rate_t tsig_audio_rate(const char *);
void tsig_audio_fill_buffer(tsig_audio_format_t, uint32_t, uint64_t, uint8_t[],
                            double[]);
bool tsig_audio_is_cpu_le(void);
