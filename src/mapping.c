// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * mapping.c: Key-value mappings.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "mapping.h"

#include <stddef.h>

/**
 * Match key to value for a string-integral mapping.
 *
 * @param mapping Empty mapping-terminated array of string-integral mappings.
 * @param key Key to match, case-insensitive.
 * @return Matching value, or -1 if no matching value was found.
 */
long tsig_mapping_match_key(const tsig_mapping_t mapping[], const char *key) {
  for (int i = 0; mapping[i].key || mapping[i].value; i++) {
    /* strcasecmp() is non-standard, so do it ourselves. */
    const char *s1 = mapping[i].key;
    const char *s2 = key;

    for (; *s1 && *s2; s1++, s2++)
      if ((*s1 <= 'Z' ? *s1 + 'a' - 'A' : *s1) !=
          (*s2 <= 'Z' ? *s2 + 'a' - 'A' : *s2))
        break;

    if (*s1 == *s2)
      return mapping[i].value;
  }

  return -1;
}

/**
 * Match value to key for a string-integral mapping.
 *
 * @param mapping Empty mapping-terminated array of string-integral mappings.
 * @param value Value to match.
 * @return Matching key, or NULL if no matching key was found.
 */
const char *tsig_mapping_match_value(const tsig_mapping_t mapping[],
                                     long value) {
  for (int i = 0; mapping[i].key || mapping[i].value; i++)
    if (value == mapping[i].value)
      return mapping[i].key;

  return NULL;
}

/**
 * Match key to value for an integral-integral mapping.
 *
 * @param mapping Empty mapping-terminated array of integral-integral mappings.
 * @param key Key to match.
 * @return Matching value, or -1 if no matching value was found.
 */
long tsig_mapping_nn_match_key(const tsig_mapping_nn_t mapping[], long key) {
  for (int i = 0; mapping[i].key || mapping[i].value; i++)
    if (key == mapping[i].key)
      return mapping[i].value;

  return -1;
}

/**
 * Match value to key for an integral-integral mapping.
 *
 * @param mapping Empty mapping-terminated array of integral-integral mappings.
 * @param value Value to match.
 * @return Matching key, or -1 if no matching key was found.
 */
long tsig_mapping_nn_match_value(const tsig_mapping_nn_t mapping[],
                                 long value) {
  for (int i = 0; mapping[i].key || mapping[i].value; i++)
    if (value == mapping[i].value)
      return mapping[i].key;

  return -1;
}
