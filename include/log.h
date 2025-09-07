/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * log.h: Header for logging facilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#pragma once

#include <syslog.h>

#include <stdbool.h>
#include <stdio.h>

/** Status line buffer size. */
#define TSIG_LOG_STATUS_LINE_SIZE 256

/** Maximum status line count. */
#define TSIG_LOG_STATUS_LINES 4

/** printf(3)-like syslog-compatible logging macros. */
#ifdef TSIG_DEBUG
#define tsig_log_with_level(n, ...)                            \
  do {                                                         \
    if (log->level >= (n))                                     \
      tsig_log_msg(log, (n), __FILE__, __LINE__, __VA_ARGS__); \
  } while (0)
#else
#define tsig_log_with_level(n, ...)                 \
  do {                                              \
    if (log->level >= (n))                          \
      tsig_log_msg(log, (n), NULL, 0, __VA_ARGS__); \
  } while (0)
#endif /* TSIG_DEBUG */

#define tsig_log_err(...)  tsig_log_with_level(LOG_ERR, __VA_ARGS__)
#define tsig_log_warn(...) tsig_log_with_level(LOG_WARNING, __VA_ARGS__)
#define tsig_log_note(...) tsig_log_with_level(LOG_NOTICE, __VA_ARGS__)
#define tsig_log(...)      tsig_log_with_level(LOG_INFO, __VA_ARGS__)
#define tsig_log_dbg(...)  tsig_log_with_level(LOG_DEBUG, __VA_ARGS__)

/** printf(3)-like logging macro for a TTY-only message. */
#ifdef TSIG_DEBUG
#define tsig_log_tty_with_status(n, ...)                           \
  do {                                                             \
    if (log->level >= LOG_INFO && log->console)                    \
      tsig_log_msg_tty(log, (n), __FILE__, __LINE__, __VA_ARGS__); \
  } while (0)
#else
#define tsig_log_tty_with_status(n, ...)                \
  do {                                                  \
    if (log->level >= LOG_INFO && log->console)         \
      tsig_log_msg_tty(log, (n), NULL, 0, __VA_ARGS__); \
  } while (0)
#endif /* TSIG_DEBUG */

#define tsig_log_tty(...) tsig_log_tty_with_status(0, __VA_ARGS__)

/** printf(3)-like logging macro for a TTY-only status line. */
#define tsig_log_status(n, ...)                                       \
  do {                                                                \
    if (log->have_status && 1 <= (n) && (n) <= TSIG_LOG_STATUS_LINES) \
      tsig_log_tty_with_status((n), __VA_ARGS__);                     \
  } while (0)

/** Logging context. */
typedef struct tsig_log {
  int level; /** Maximum log level. */

  bool console;       /** Whether to emit logs to stdout/stderr. */
  bool is_stdout_tty; /** Whether stdout is a TTY. */
  bool is_stderr_tty; /** Whether stderr is a TTY. */
  FILE *log_file;     /** Log file. Will emit logs to it if not NULL. */
  bool syslog;        /** Whether to emit logs to syslog. */

  bool have_status;                       /** Whether status area is enabled. */
  int status_lines;                       /** Status area line count. */
  char status_line[TSIG_LOG_STATUS_LINES] /** Status lines.  */
                  [TSIG_LOG_STATUS_LINE_SIZE];
} tsig_log_t;

void tsig_log_init(tsig_log_t *log);
void tsig_log_finish_init(tsig_log_t *log, char log_file[], bool syslog,
                          bool verbose, bool quiet);
void tsig_log_msg(tsig_log_t *log, int level, const char *src_file,
                  int src_line, const char *fmt, ...);
void tsig_log_msg_tty(tsig_log_t *log, int status_line, const char *src_file,
                      int src_line, const char *fmt, ...);
void tsig_log_deinit(tsig_log_t *log);
void tsig_log_tty_enable_echo(void);
void tsig_log_tty_disable_echo(void);
