// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * util.c: Miscellaneous utilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "util.h"

/**
 * Compare strings, ignoring differences in case.
 *
 * Similar to strcasecmp(), but that's non-standard and gives undefined
 * results if not operating on two well-formed strings, so do it ourselves.
 *
 * @param s1 First string to compare.
 * @param s2 Second string to compare.
 * @return If operating on well-formed strings, the difference between
 *  the two strings (0 indicates no difference), or -1 otherwise.
 */
int tsig_util_strcasecmp(const char *s1, const char *s2) {
  if (!s1 || !s2)
    return -1;

  for (; *s1 && *s2; s1++, s2++)
    if ((*s1 <= 'Z' ? *s1 + 'a' - 'A' : *s1) !=
        (*s2 <= 'Z' ? *s2 + 'a' - 'A' : *s2))
      break;

  return *s1 - *s2;
}
