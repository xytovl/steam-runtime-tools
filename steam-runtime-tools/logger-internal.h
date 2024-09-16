/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <syslog.h>

#include "libglnx.h"

#include "steam-runtime-tools/env-overlay-internal.h"

/* Default least important level to write to files */
#define SRT_SYSLOG_LEVEL_DEFAULT_FILE LOG_DEBUG
/* Default least important level to write to the journal */
#define SRT_SYSLOG_LEVEL_DEFAULT_JOURNAL LOG_DEBUG
/* Default least important level to write to the terminal */
#define SRT_SYSLOG_LEVEL_DEFAULT_TERMINAL LOG_INFO

/* Default level to assign to lines with no <N> prefix */
#define SRT_SYSLOG_LEVEL_DEFAULT_LINE LOG_INFO

gboolean _srt_syslog_level_parse (const char *s,
                                  int *out_level,
                                  GError **error);

typedef struct _SrtLogger SrtLogger;
typedef struct _SrtLoggerClass SrtLoggerClass;

#define SRT_TYPE_LOGGER \
  (_srt_logger_get_type ())
#define SRT_LOGGER(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), SRT_TYPE_LOGGER, SrtLogger))
#define SRT_IS_LOGGER(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), SRT_TYPE_LOGGER))
#define SRT_LOGGER_GET_CLASS(o) \
  (G_TYPE_INSTANCE_GET_CLASS ((o), SRT_TYPE_LOGGER, SrtLoggerClass))
#define SRT_LOGGER_CLASS(c) \
  (G_TYPE_CHECK_CLASS_CAST ((c), SRT_TYPE_LOGGER, SrtLoggerClass))
#define SRT_IS_LOGGER_CLASS(c) \
  (G_TYPE_CHECK_CLASS_TYPE ((c), SRT_TYPE_LOGGER))

GType _srt_logger_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtLogger, g_object_unref)

SrtLogger *_srt_logger_new_take (gchar *argv0,
                                 gboolean background,
                                 int default_line_level,
                                 gchar *filename,
                                 int file_fd,
                                 int file_level,
                                 gchar *identifier,
                                 gboolean journal,
                                 int journal_fd,
                                 int journal_level,
                                 gchar *log_dir,
                                 goffset max_bytes,
                                 int original_stderr,
                                 gboolean parse_level_prefix,
                                 gboolean sh_syntax,
                                 gboolean terminal,
                                 int terminal_fd,
                                 int terminal_level,
                                 gboolean timestamps);

gboolean _srt_logger_run_subprocess (SrtLogger *self,
                                     const char *logger,
                                     gboolean consume_stdin,
                                     const char * const *envp,
                                     int *original_stdout,
                                     GError **error);

gboolean _srt_logger_process (SrtLogger *self,
                              int *original_stdout,
                              GError **error);

SrtEnvOverlay *_srt_logger_get_environ (SrtLogger *self);
