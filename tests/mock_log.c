// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * mock_log.c: Mock logging facilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "log.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

void __wrap_tsig_log_init(tsig_log_t *log) {
  (void)log; /* Suppress unused parameter warning. */
}

void __wrap_tsig_log_finish_init(tsig_log_t *log, char log_file[], bool syslog,
                                 bool verbose, bool quiet) {
  (void)log;      /* Suppress unused parameter warning. */
  (void)log_file; /* Suppress unused parameter warning. */
  (void)syslog;   /* Suppress unused parameter warning. */
  (void)verbose;  /* Suppress unused parameter warning. */
  (void)quiet;    /* Suppress unused parameter warning. */
}

void __wrap_tsig_log_msg(tsig_log_t *log, int level, const char *src_file,
                         int src_line, const char *fmt, ...) {
  (void)log;      /* Suppress unused parameter warning. */
  (void)level;    /* Suppress unused parameter warning. */
  (void)src_file; /* Suppress unused parameter warning. */
  (void)src_line; /* Suppress unused parameter warning. */
  (void)fmt;      /* Suppress unused parameter warning. */
}

void __wrap_tsig_log_msg_tty(tsig_log_t *log, const char *src_file,
                             int src_line, const char *fmt, ...) {
  (void)log;      /* Suppress unused parameter warning. */
  (void)src_file; /* Suppress unused parameter warning. */
  (void)src_line; /* Suppress unused parameter warning. */
  (void)fmt;      /* Suppress unused parameter warning. */
}

void __wrap_tsig_log_status_impl(tsig_log_t *log, int status_line,
                                 const char *src_file, int src_line,
                                 const char *fmt, ...) {
  (void)log;         /* Suppress unused parameter warning. */
  (void)status_line; /* Suppress unused parameter warning. */
  (void)src_file;    /* Suppress unused parameter warning. */
  (void)src_line;    /* Suppress unused parameter warning. */
  (void)fmt;         /* Suppress unused parameter warning. */
}

void __wrap_tsig_log_status_print_impl(tsig_log_t *log) {
  (void)log; /* Suppress unused parameter warning. */
}

void __wrap_tsig_log_deinit(tsig_log_t *log) {
  (void)log; /* Suppress unused parameter warning. */
}

void __wrap_tsig_log_tty_enable_echo(void) {
}

void __wrap_tsig_log_tty_disable_echo(void) {
}
