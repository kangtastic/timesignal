// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * audio.c: Generalized audio facilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "audio.h"
#include "mapping.h"

#include <stdbool.h>
#include <stddef.h>
#include <limits.h>

/** Sample format names. */
static const tsig_mapping_t audio_formats[] = {
    {"S16", TSIG_AUDIO_FORMAT_S16},
    {"S16_LE", TSIG_AUDIO_FORMAT_S16_LE},
    {"S16_BE", TSIG_AUDIO_FORMAT_S16_BE},
    {"S24", TSIG_AUDIO_FORMAT_S24},
    {"S24_LE", TSIG_AUDIO_FORMAT_S24_LE},
    {"S24_BE", TSIG_AUDIO_FORMAT_S24_BE},
    {"S32", TSIG_AUDIO_FORMAT_S32},
    {"S32_LE", TSIG_AUDIO_FORMAT_S32_LE},
    {"S32_BE", TSIG_AUDIO_FORMAT_S32_BE},
    {"U16", TSIG_AUDIO_FORMAT_U16},
    {"U16_LE", TSIG_AUDIO_FORMAT_U16_LE},
    {"U16_BE", TSIG_AUDIO_FORMAT_U16_BE},
    {"U24", TSIG_AUDIO_FORMAT_U24},
    {"U24_LE", TSIG_AUDIO_FORMAT_U24_LE},
    {"U24_BE", TSIG_AUDIO_FORMAT_U24_BE},
    {"U32", TSIG_AUDIO_FORMAT_U32},
    {"U32_LE", TSIG_AUDIO_FORMAT_U32_LE},
    {"U32_BE", TSIG_AUDIO_FORMAT_U32_BE},
    {"FLOAT", TSIG_AUDIO_FORMAT_FLOAT},
    {"FLOAT_LE", TSIG_AUDIO_FORMAT_FLOAT_LE},
    {"FLOAT_BE", TSIG_AUDIO_FORMAT_FLOAT_BE},
    {"FLOAT64", TSIG_AUDIO_FORMAT_FLOAT64},
    {"FLOAT64_LE", TSIG_AUDIO_FORMAT_FLOAT64_LE},
    {"FLOAT64_BE", TSIG_AUDIO_FORMAT_FLOAT64_BE},
    {NULL, 0},
};

/** Sample rate names. */
static const tsig_mapping_t audio_rates[] = {
    {"44100", TSIG_AUDIO_RATE_44100},
    {"48000", TSIG_AUDIO_RATE_48000},
    {"88200", TSIG_AUDIO_RATE_88200},
    {"96000", TSIG_AUDIO_RATE_96000},
    {"176400", TSIG_AUDIO_RATE_176400},
    {"192000", TSIG_AUDIO_RATE_192000},
    {"352800", TSIG_AUDIO_RATE_352800},
    {"384000", TSIG_AUDIO_RATE_384000},
    {NULL, 0},
};

/** Check if audio format is floating-point. */
static bool audio_format_is_float(tsig_audio_format_t format) {
  return format == TSIG_AUDIO_FORMAT_FLOAT ||
         format == TSIG_AUDIO_FORMAT_FLOAT_LE ||
         format == TSIG_AUDIO_FORMAT_FLOAT_BE ||
         format == TSIG_AUDIO_FORMAT_FLOAT64 ||
         format == TSIG_AUDIO_FORMAT_FLOAT64_LE ||
         format == TSIG_AUDIO_FORMAT_FLOAT64_BE;
}

/** Check if audio format is signed. */
static bool audio_format_is_signed(tsig_audio_format_t format) {
  return format == TSIG_AUDIO_FORMAT_S16 ||
         format == TSIG_AUDIO_FORMAT_S16_LE ||
         format == TSIG_AUDIO_FORMAT_S16_BE ||
         format == TSIG_AUDIO_FORMAT_S24 ||
         format == TSIG_AUDIO_FORMAT_S24_LE ||
         format == TSIG_AUDIO_FORMAT_S24_BE ||
         format == TSIG_AUDIO_FORMAT_S32 ||
         format == TSIG_AUDIO_FORMAT_S32_LE ||
         format == TSIG_AUDIO_FORMAT_S32_BE;
}

/** Check if audio format is little-endian. */
static bool audio_format_is_le(tsig_audio_format_t format) {
  bool is_le_unspecified =
      format == TSIG_AUDIO_FORMAT_S16 || format == TSIG_AUDIO_FORMAT_S24 ||
      format == TSIG_AUDIO_FORMAT_S32 || format == TSIG_AUDIO_FORMAT_U16 ||
      format == TSIG_AUDIO_FORMAT_U24 || format == TSIG_AUDIO_FORMAT_U32 ||
      format == TSIG_AUDIO_FORMAT_FLOAT || format == TSIG_AUDIO_FORMAT_FLOAT64;

  return (is_le_unspecified && tsig_audio_is_cpu_le()) ||
         format == TSIG_AUDIO_FORMAT_S16_LE ||
         format == TSIG_AUDIO_FORMAT_S24_LE ||
         format == TSIG_AUDIO_FORMAT_S32_LE ||
         format == TSIG_AUDIO_FORMAT_U16_LE ||
         format == TSIG_AUDIO_FORMAT_U24_LE ||
         format == TSIG_AUDIO_FORMAT_U32_LE ||
         format == TSIG_AUDIO_FORMAT_FLOAT_LE ||
         format == TSIG_AUDIO_FORMAT_FLOAT64_LE;
}

/** Find width of audio format. */
static size_t audio_format_width(tsig_audio_format_t format) {
  return (format == TSIG_AUDIO_FORMAT_S16 ||
          format == TSIG_AUDIO_FORMAT_S16_LE ||
          format == TSIG_AUDIO_FORMAT_S16_BE ||
          format == TSIG_AUDIO_FORMAT_U16 ||
          format == TSIG_AUDIO_FORMAT_U16_LE ||
          format == TSIG_AUDIO_FORMAT_U16_BE)
             ? 2
         : (format == TSIG_AUDIO_FORMAT_S24 ||
            format == TSIG_AUDIO_FORMAT_S24_LE ||
            format == TSIG_AUDIO_FORMAT_S24_BE ||
            format == TSIG_AUDIO_FORMAT_U24 ||
            format == TSIG_AUDIO_FORMAT_U24_LE ||
            format == TSIG_AUDIO_FORMAT_U24_BE)
             ? 3
         : (format == TSIG_AUDIO_FORMAT_S32 ||
            format == TSIG_AUDIO_FORMAT_S32_LE ||
            format == TSIG_AUDIO_FORMAT_S32_BE ||
            format == TSIG_AUDIO_FORMAT_U32 ||
            format == TSIG_AUDIO_FORMAT_U32_LE ||
            format == TSIG_AUDIO_FORMAT_U32_BE ||
            format == TSIG_AUDIO_FORMAT_FLOAT ||
            format == TSIG_AUDIO_FORMAT_FLOAT_LE ||
            format == TSIG_AUDIO_FORMAT_FLOAT_BE)
             ? 4
         : (format == TSIG_AUDIO_FORMAT_FLOAT64 ||
            format == TSIG_AUDIO_FORMAT_FLOAT64_LE ||
            format == TSIG_AUDIO_FORMAT_FLOAT64_BE)
             ? 8
             : 0; /* TSIG_AUDIO_FORMAT_UNKNOWN */
}

/** Find physical width of audio format. */
static size_t audio_format_phys_width(tsig_audio_format_t format) {
  return (format == TSIG_AUDIO_FORMAT_S16 ||
          format == TSIG_AUDIO_FORMAT_S16_LE ||
          format == TSIG_AUDIO_FORMAT_S16_BE ||
          format == TSIG_AUDIO_FORMAT_U16 ||
          format == TSIG_AUDIO_FORMAT_U16_LE ||
          format == TSIG_AUDIO_FORMAT_U16_BE)
             ? 2
         : (format == TSIG_AUDIO_FORMAT_S24 ||
            format == TSIG_AUDIO_FORMAT_S24_LE ||
            format == TSIG_AUDIO_FORMAT_S24_BE ||
            format == TSIG_AUDIO_FORMAT_U24 ||
            format == TSIG_AUDIO_FORMAT_U24_LE ||
            format == TSIG_AUDIO_FORMAT_U24_BE ||
            format == TSIG_AUDIO_FORMAT_S32 ||
            format == TSIG_AUDIO_FORMAT_S32_LE ||
            format == TSIG_AUDIO_FORMAT_S32_BE ||
            format == TSIG_AUDIO_FORMAT_U32 ||
            format == TSIG_AUDIO_FORMAT_U32_LE ||
            format == TSIG_AUDIO_FORMAT_U32_BE ||
            format == TSIG_AUDIO_FORMAT_FLOAT ||
            format == TSIG_AUDIO_FORMAT_FLOAT_LE ||
            format == TSIG_AUDIO_FORMAT_FLOAT_BE)
             ? 4
         : (format == TSIG_AUDIO_FORMAT_FLOAT64 ||
            format == TSIG_AUDIO_FORMAT_FLOAT64_LE ||
            format == TSIG_AUDIO_FORMAT_FLOAT64_BE)
             ? 8
             : 0; /* TSIG_AUDIO_FORMAT_UNKNOWN */
}

/**
 * Match a sample format name to its value.
 *
 * @param name Sample format name.
 * @return Sample format value, or TSIG_AUDIO_FORMAT_UNKNOWN if invalid.
 */
tsig_audio_format_t tsig_audio_format(const char *name) {
  tsig_audio_format_t value = tsig_mapping_match_key(audio_formats, name);
  return value < 0 ? TSIG_AUDIO_FORMAT_UNKNOWN : value;
}

/**
 * Match a sample format value to its name.
 *
 * @param format Sample format value.
 * @return Sample format name, or NULL if invalid.
 */
const char *tsig_audio_format_name(tsig_audio_format_t format) {
  return tsig_mapping_match_value(audio_formats, format);
}

/**
 * Match a sample rate name to its value.
 *
 * @param name Sample rate name.
 * @return Sample rate value, or TSIG_AUDIO_RATE_UNKNOWN if invalid.
 */
tsig_audio_rate_t tsig_audio_rate(const char *name) {
  tsig_audio_rate_t value = tsig_mapping_match_key(audio_rates, name);
  return value < 0 ? TSIG_AUDIO_RATE_UNKNOWN : value;
}

/**
 * Fill an output audio buffer with generated samples.
 *
 * @param format Output sample format.
 * @param channels Output channel count.
 * @param size Sample count.
 * @param buf Output audio buffer.
 * @param cb_buf Buffer with generated 1ch 64-bit float samples.
 */
void tsig_audio_fill_buffer(tsig_audio_format_t format, uint32_t channels,
                            uint64_t size, uint8_t buf[], double cb_buf[]) {
  bool is_swap = tsig_audio_is_cpu_le() != audio_format_is_le(format);
  size_t phys_width = audio_format_phys_width(format);
  bool is_signed = audio_format_is_signed(format);
  bool is_float = audio_format_is_float(format);
  size_t width = audio_format_width(format);
  uint64_t *buf_u64 = (uint64_t *)buf;
  uint32_t *buf_u32 = (uint32_t *)buf;
  uint16_t *buf_u16 = (uint16_t *)buf;
  union {
    uint64_t u64;
    uint32_t u32;
    uint16_t u16;
    int64_t i64;
    double f64;
    float f32;
  } n;

  if (!phys_width || !width)
    return; /* TSIG_AUDIO_FORMAT_UNKNOWN */

  for (uint64_t i = 0; i < size; i++) {
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

    /* Convert back to the proper output format. */
    if (is_float && width == 8)
      n.f64 = (double)n.i64 / -INT16_MIN;
    else if (is_float) /* width == 4 */
      n.f32 = (float)n.i64 / -INT16_MIN;
    else if (width == 4)
      n.u32 = n.u64 << 16;
    else if (width == 3)
      n.u32 = n.u64 << 8;
    else /* width == 2 */
      n.u16 = n.u64;

    /* Write the current sample value for all interleaved channels. */
    for (uint32_t c = 0; c < channels; c++)
      if (phys_width == 8)
        *buf_u64++ = is_swap ? __builtin_bswap64(n.u64) : n.u64;
      else if (phys_width == 4)
        *buf_u32++ = is_swap ? __builtin_bswap32(n.u32) : n.u32;
      else /* phys_width == 2 */
        *buf_u16++ = is_swap ? __builtin_bswap16(n.u16) : n.u16;
  }
}

/**
 * Check if the current machine is little-endian.
 *
 * @return Whether the current machine is little-endian.
 */
bool tsig_audio_is_cpu_le(void) {
  union {
    uint32_t u32;
    uint8_t u8;
  } n = {.u32 = 1};

  return n.u8 == 1;
}
