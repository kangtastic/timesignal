// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * log.c: Logging facilities.
 *
 * This file is part of timesignal.
 *
 * Copyright © 2025 James Seo <james@equiv.tech>
 */

#include "log.h"

#include "defaults.h"

#include <syslog.h>
#include <unistd.h>

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/** Timestamp buffer size. */
#define TSIG_LOG_TIMESTAMP_SIZE 128

/** Minimum __FILE__:__LINE__ marker width. */
#define TSIG_LOG_SRC_INFO_MIN_WIDTH 10

/** Log level descriptions for non-TTY/log file/syslog output.  */
static const char *log_descs[] = {
    NULL,        /* LOG_EMERG, unused */
    NULL,        /* LOG_ALERT, unused */
    NULL,        /* LOG_CRIT, unused */
    "error: ",   /* LOG_ERROR */
    "warning: ", /* LOG_WARNING */
    "notice: ",  /* LOG_NOTICE */
    "",          /* LOG_INFO, left blank */
    "debug: ",   /* LOG_DEBUG */
};

/** Colorized log level descriptions for TTY output.  */
static const char *log_descs_tty[] = {
    NULL,                         /* LOG_EMERG, unused */
    NULL,                         /* LOG_ALERT, unused */
    NULL,                         /* LOG_CRIT, unused */
    "\x1b[1;91merror:\x1b[0m ",   /* LOG_ERROR, bold, light red */
    "\x1b[1;95mwarning:\x1b[0m ", /* LOG_WARNING, bold, light magenta */
    "\x1b[1;94mnotice:\x1b[0m ",  /* LOG_NOTICE, bold, light blue */
    "",                           /* LOG_INFO, left blank */
    "",                           /* LOG_DEBUG, left blank */
};

/** Default logging context. */
static tsig_log_t log_default = {
    .level = LOG_INFO,
    .console = true,
    .log_file = NULL,
    .syslog = false,
};

/** Write a timestamp to a file. */
static void log_write_timestamp(FILE *file) {
  char timestamp[TSIG_LOG_TIMESTAMP_SIZE];
  struct timespec ts;

  if (clock_gettime(CLOCK_REALTIME, &ts))
    return;

  if (!strftime(timestamp, sizeof(timestamp), "%x %X", localtime(&ts.tv_sec)))
    return;

  fprintf(file, "%s.%.03ld | ", timestamp, ts.tv_nsec / 1000000);
}

/** Write a message to a file, possibly prefixed with source file and line. */
static void log_write_msg(FILE *file, const char *src_file, int src_line,
                          const char *desc, const char *fmt, va_list params) {
  int src_info = TSIG_LOG_SRC_INFO_MIN_WIDTH;

  if (src_file && src_line) {
    src_info -= fprintf(file, "%s:%d", src_file, src_line);
    if (src_info > 0)
      fprintf(file, "%*s", src_info, "");
    fprintf(file, " | ");
  }

  fprintf(file, "%s", desc);
  vfprintf(file, fmt, params);
  fprintf(file, "%s", "\n");

  fflush(file);
}

/** Log a message to console (stdout/stderr) or TTY. */
static void log_msg_console(tsig_log_t *log, int level, const char *src_file,
                            int src_line, const char *fmt, va_list params) {
  const char *desc;
  bool have_tty;
  FILE *file;

  /* Colorize description when logging to a TTY. */
  file = level > LOG_WARNING ? stdout : stderr;
  have_tty = (file == stdout && log->is_stdout_tty) ||
             (file == stderr && log->is_stderr_tty);
  desc = have_tty ? log_descs_tty[level] : log_descs[level];

  log_write_msg(file, src_file, src_line, desc, fmt, params);
}

#ifdef TSIG_DEBUG
/** Print initialized logging context. */
static void log_print(tsig_log_t *log) {
  tsig_log_dbg("tsig_log_t %p = {", log);
  tsig_log_dbg("  .level          = %d,", log->level);
  tsig_log_dbg("  .console        = %d,", log->console);
  tsig_log_dbg("  .is_stdout_tty  = %d,", log->is_stdout_tty);
  tsig_log_dbg("  .is_stderr_tty  = %d,", log->is_stderr_tty);
  tsig_log_dbg("  .log_file       = %p,", log->log_file);
  tsig_log_dbg("  .syslog         = %d,", log->syslog);
  tsig_log_dbg("};");
}
#endif /* TSIG_DEBUG */

/**
 * Initialize a logging context.
 *
 * @param log Uninitialized logging context.
 */
void tsig_log_init(tsig_log_t *log) {
  *log = log_default;

  log->is_stdout_tty = isatty(fileno(stdout));
  log->is_stderr_tty = isatty(fileno(stderr));
}

/**
 * Update a logging context from program configuration.
 *
 * @param log Initialized logging context.
 * @param log_file Log file. Will emit logs to it if not NULL.
 * @param syslog Whether to emit logs to syslog.
 * @param verbose Whether to emit verbose logs.
 */
void tsig_log_finish_init(tsig_log_t *log, char log_file[], bool syslog,
                          bool verbose) {
  if (verbose)
    log->level = LOG_DEBUG;

  if (syslog) {
    openlog(TSIG_DEFAULTS_NAME, LOG_PID, LOG_USER);
    log->syslog = true;
  }

  if (*log_file) {
    log->log_file = fopen(log_file, "a");

    if (!log->log_file)
      tsig_log_warn("Failed to open log file \"%s\": %s", log_file,
                    strerror(errno));
  }

#ifdef TSIG_DEBUG
  log_print(log);
#endif /* TSIG_DEBUG */
}

/**
 * Log a message.
 *
 * Depending on the circumstances, the same message is emitted
 * to the console (stdout/stderr), a log file, and/or syslog.
 *
 * @param log Initialized logging context.
 * @param level Log level.
 * @param src_file Source filename, ordinarily NULL.
 * @param src_line Source file line number, ordinarily 0.
 * @param fmt Format string.
 */
__attribute__((format(printf, 5, 6))) void tsig_log_msg(tsig_log_t *log,
                                                        int level,
                                                        const char *src_file,
                                                        int src_line,
                                                        const char *fmt, ...) {
  va_list cparams;
  va_list fparams;
  va_list sparams;
  va_list params;

  va_start(params, fmt);

  if (log->console) {
    va_copy(cparams, params);
    log_msg_console(log, level, src_file, src_line, fmt, cparams);
    va_end(cparams);
  }

  if (log->log_file) {
    va_copy(fparams, params);
    log_write_timestamp(log->log_file);
    log_write_msg(log->log_file, src_file, src_line, log_descs[level], fmt,
                  fparams);
    va_end(fparams);
  }

  if (log->syslog) {
    va_copy(sparams, params);
    vsyslog(level, fmt, sparams);
    va_end(sparams);
  }

  va_end(params);
}

/**
 * Log a message to a TTY only.
 *
 * @param log Initialized logging context.
 * @param src_file Source file name, ordinarily NULL.
 * @param src_line Source line number, ordinarily 0.
 * @param fmt Format string.
 */
__attribute__((format(printf, 4, 5))) void tsig_log_msg_tty(
    tsig_log_t *log, const char *src_file, int src_line, const char *fmt, ...) {
  va_list params;

  /* Log level is always LOG_INFO, output file is always stdout. */
  if (!log->is_stdout_tty)
    return;

  va_start(params, fmt);
  log_msg_console(log, LOG_INFO, src_file, src_line, fmt, params);
  va_end(params);
}

/**
 * Deinitialize logging context.
 *
 * @param log Initialized logging context.
 */
void tsig_log_deinit(tsig_log_t *log) {
  if (log->log_file)
    fclose(log->log_file);
}
