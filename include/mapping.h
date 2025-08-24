/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * mapping.h: Header for key-value mappings.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#pragma once

/** String-integral mapping. */
typedef struct tsig_mapping {
  const char *key;
  const long value;
} tsig_mapping_t;

/** Integral-integral mapping. */
typedef struct tsig_mapping_nn {
  const long key;
  const long value;
} tsig_mapping_nn_t;

long tsig_mapping_match_key(const tsig_mapping_t mapping[], const char *key);
const char *tsig_mapping_match_value(const tsig_mapping_t mapping[],
                                     long value);
long tsig_mapping_nn_match_key(const tsig_mapping_nn_t mapping[], long key);
long tsig_mapping_nn_match_value(const tsig_mapping_nn_t mapping[], long value);
