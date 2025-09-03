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

/** Buffer sizes. */
#define TSIG_LOG_TIMESTAMP_SIZE 128
#define TSIG_LOG_TTY_NAME_SIZE  256

/** Minimum __FILE__:__LINE__ marker width. */
#define TSIG_LOG_SRC_INFO_MIN_WIDTH 10

/** Escape strings and escape format strings. */
static const char *log_esc_line_move_up_fmt = "\x1b[%dA";
static const char *log_esc_line_scroll_up = "\x1bM";
static const char *log_esc_line_clear = "\x1b[2K";

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
    .have_status = false,
    .status_lines = 0,
    .status_line = {{""}},
};

/** Find next write position and remaining space in a status line buffer. */
static void log_status_line_find_write_pos(char buf[], int len, char **wr,
                                           int *remain) {
  if (len < TSIG_LOG_STATUS_LINE_SIZE) {
    *wr = &buf[len];
    *remain = TSIG_LOG_STATUS_LINE_SIZE - len;
  } else {
    *wr = NULL;
    *remain = 0;
  }
}

/**
 * Write a message into a status line buffer,
 * possibly prefixed with source file and line.
 */
static int log_status_line_write_msg(char buf[], int len, const char *src_file,
                                     int src_line, const char *fmt,
                                     va_list params) {
  int src_info;
  int spaces;
  int remain;
  char *wr;

  if (src_file && src_line) {
    log_status_line_find_write_pos(buf, len, &wr, &remain);
    src_info = snprintf(wr, remain, "%s:%d", src_file, src_line);
    len += src_info;

    if (src_info < TSIG_LOG_SRC_INFO_MIN_WIDTH) {
      spaces = TSIG_LOG_SRC_INFO_MIN_WIDTH - src_info;
      log_status_line_find_write_pos(buf, len, &wr, &remain);
      len += snprintf(wr, remain, "%*s", spaces, "");
    }

    log_status_line_find_write_pos(buf, len, &wr, &remain);
    len += snprintf(wr, remain, " | ");
  }

  log_status_line_find_write_pos(buf, len, &wr, &remain);
  len += snprintf(wr, remain, "%s", log_descs_tty[LOG_INFO]);

  log_status_line_find_write_pos(buf, len, &wr, &remain);
  len += vsnprintf(wr, remain, fmt, params);

  return len;
}

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

  /*
   * If the status area is enabled and present, write the message to the
   * gap before the status area, then rewrite the gap and the status area.
   * (stdout and stderr were merged; the output file will always be stdout.)
   *
   * e.g. not status area        ->  not status area
   *                                 not status area (this message)
   *      status line n
   *      status line 1<cursor>      status line n
   *                                 status line 1<cursor>
   */

  if (log->status_lines) {
    fprintf(stdout, log_esc_line_move_up_fmt, log->status_lines);
    fprintf(stdout, "%s", "\r");
  }

  log_write_msg(file, src_file, src_line, desc, fmt, params);

  /* Reconstruct status area if necessary. */
  if (log->status_lines) {
    fprintf(stdout, "%s\r", log_esc_line_clear);
    for (int line = log->status_lines; line; line--)
      fprintf(stdout, "\n%s\r%s", log_esc_line_clear,
              log->status_line[line - 1]);
    fflush(stdout);
  }
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
  tsig_log_dbg("  .have_status    = %d,", log->have_status);
  tsig_log_dbg("  .status_lines   = %d,", log->status_lines);
  tsig_log_dbg("  .status_line    = %p,", &log->status_line);
  tsig_log_dbg("};");
}
#endif /* TSIG_DEBUG */

/**
 * Initialize a logging context.
 *
 * @param log Uninitialized logging context.
 */
void tsig_log_init(tsig_log_t *log) {
  char stdout_tty[TSIG_LOG_TTY_NAME_SIZE];
  char stderr_tty[TSIG_LOG_TTY_NAME_SIZE];

  *log = log_default;

  log->is_stdout_tty = isatty(fileno(stdout));
  log->is_stderr_tty = isatty(fileno(stderr));

  /*
   * If stdout and stderr are connected to the same TTY and we're
   * able to redirect stderr to stdout, enable the status area.
   */

  if (!log->is_stdout_tty || !log->is_stderr_tty)
    return;

  if (ttyname_r(fileno(stdout), stdout_tty, sizeof(stdout_tty)) ||
      ttyname_r(fileno(stderr), stderr_tty, sizeof(stderr_tty)) ||
      strcmp(stdout_tty, stderr_tty))
    return;

  if (dup2(fileno(stdout), fileno(stderr)) < 0)
    return;

  log->have_status = true;
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
 * @note Direct use skips safety checks; use via a logging macro instead.
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
 * @note Direct use skips safety checks; use via a logging macro instead.
 *
 * @param log Initialized logging context.
 * @param status_line Status line number, or 0 if not a status line.
 * @param src_file Source file name, ordinarily NULL.
 * @param src_line Source line number, ordinarily 0.
 * @param fmt Format string.
 */
__attribute__((format(printf, 5, 6))) void tsig_log_msg_tty(
    tsig_log_t *log, int status_line, const char *src_file, int src_line,
    const char *fmt, ...) {
  va_list params;
  char *buf;
  int len;

  /* Log level is always LOG_INFO, output file is always stdout. */
  if (!log->is_stdout_tty)
    return;

  /* If not a status line, log message as usual and exit. */
  if (!status_line) {
    va_start(params, fmt);
    log_msg_console(log, LOG_INFO, src_file, src_line, fmt, params);
    va_end(params);
    return;
  }

  /* Write the status line to the corresponding buffer. */
  va_start(params, fmt);
  buf = log->status_line[status_line - 1];
  len = log_status_line_write_msg(buf, 0, src_file, src_line, fmt, params);
  va_end(params);

  /* Truncate the line if it is too long (unlikely). */
  if (len > TSIG_LOG_STATUS_LINE_SIZE - 1) {
    buf[TSIG_LOG_STATUS_LINE_SIZE - 4] = '.';
    buf[TSIG_LOG_STATUS_LINE_SIZE - 3] = '.';
    buf[TSIG_LOG_STATUS_LINE_SIZE - 2] = '.';
    buf[TSIG_LOG_STATUS_LINE_SIZE - 1] = '\0';
  }

  /*
   * Write the status area, separated between normal messages by a gap.
   * (stdout and stderr were merged; the output file will always be stdout.)
   *
   * e.g. not status area        ->  not status area
   *      <cursor>
   *                                 status line n
   *                                 status line 1<cursor>
   */

  for (; log->status_lines < status_line; log->status_lines++)
    fprintf(stdout, "%s", "\n");

  fprintf(stdout, log_esc_line_move_up_fmt, log->status_lines);
  fprintf(stdout, "%s", "\r");

  for (int line = log->status_lines; line; line--)
    fprintf(stdout, "\n%s\r%s", log_esc_line_clear, log->status_line[line - 1]);

  fflush(stdout);
}

/**
 * Deinitialize logging context.
 *
 * @param log Initialized logging context.
 */
void tsig_log_deinit(tsig_log_t *log) {
  if (log->log_file)
    fclose(log->log_file);

  /*
   * If the status area is enabled and present, clear the status area
   * and place the cursor at the first column of the gap before it.
   * (stdout and stderr were merged; the output file will always be stdout.)
   *
   * e.g. not status area        ->  not status area
   *                                 <cursor>
   *      status line n
   *      status line 1<cursor>
   */

  if (log->status_lines) {
    for (int line = log->status_lines; line > 0; line--)
      fprintf(stdout, "%s\r%s", log_esc_line_clear, log_esc_line_scroll_up);
    fflush(stdout);
  }
}
