// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * util.c: Miscellaneous utilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "util.h"

#include "defaults.h"

#include <libgen.h>

#include <stdlib.h>

/**
 * Find the name of the program during runtime.
 *
 * Should give similar results to BSD's getprogname(3), glibc's
 * program_invocation_name(3), or the usual argv[0] trickery.
 *
 * @param[out] progname Output buffer at least PATH_MAX in size.
 */
void tsig_util_getprogname(char progname[]) {
  char *name = TSIG_DEFAULTS_NAME;
  char *tmp;
  char *wr;

  /*
   * Program name is basename(realpath(<exe name>)). Caveats:
   *
   *  - realpath(3) returns NULL if its result cannot fit in PATH_MAX.
   *  - The version of basename(3) we're using is the POSIX version from
   *    <libgen.h>, so it may return "/", it may modify its argument, and
   *    its return value may be either a static buffer or inside its argument.
   *  - strcpy(3) doesn't like copying within the same buffer.
   */

  if (realpath("/proc/self/exe", progname)) {
    tmp = basename(progname);
    if (*tmp && *tmp != '/' && *tmp != '.')
      name = tmp;
  }

  wr = progname;

  while (*name)
    *wr++ = *name++;

  *wr = '\0';
}

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
