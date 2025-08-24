// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * backend.c: Audio backend facilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "backend.h"

#include "mapping.h"

#include <stddef.h>

/** Audio backend names. */
static const tsig_mapping_t backend_backends[] = {

#ifdef TSIG_HAVE_PIPEWIRE
    {"PipeWire", TSIG_BACKEND_PIPEWIRE},
    {"pw", TSIG_BACKEND_PIPEWIRE},
#endif /* TSIG_HAVE_PIPEWIRE */

#ifdef TSIG_HAVE_ALSA
    {"ALSA", TSIG_BACKEND_ALSA},
#endif /* TSIG_HAVE_ALSA */

    {NULL, 0},
};

/**
 * Match an audio backend name to its value.
 *
 * @param name Audio backend name.
 * @return Audio backend value, or TSIG_AUDIO_FORMAT_UNKNOWN if invalid.
 */
tsig_backend_t tsig_backend(const char *name) {
  tsig_backend_t value = tsig_mapping_match_key(backend_backends, name);
  return value < 0 ? TSIG_BACKEND_UNKNOWN : value;
}

/**
 * Match an audio backend value to its name.
 *
 * @param backend Audio backend value.
 * @return Time station name, or NULL if invalid.
 */
const char *tsig_backend_name(tsig_backend_t backend) {
  return tsig_mapping_match_value(backend_backends, backend);
}
