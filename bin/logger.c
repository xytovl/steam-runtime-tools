/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <locale.h>

#include "steam-runtime-tools/glib-backports-internal.h"

#include <steam-runtime-tools/steam-runtime-tools.h>
#include "steam-runtime-tools/launcher-internal.h"
#include "steam-runtime-tools/log-internal.h"
#include "steam-runtime-tools/logger-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#define THIS_PROGRAM "srt-logger"

#define MEBIBYTE 1024 * 1024

static gboolean opt_auto_terminal = TRUE;
static gboolean opt_background = FALSE;
static gboolean opt_exec_fallback = FALSE;
static gchar *opt_filename = NULL;
static gchar *opt_identifier = NULL;
static int opt_journal_fd = -1;
static gchar *opt_log_directory = NULL;
static int opt_log_fd = -1;
static goffset opt_max_bytes = 8 * MEBIBYTE;
static gboolean opt_sh_syntax = FALSE;
static int opt_terminal_fd = -1;
static gboolean opt_use_journal = FALSE;
static unsigned opt_verbose = 0;
static gboolean opt_version = FALSE;

static gboolean
opt_verbose_cb (const char *name,
                const char *value,
                gpointer data,
                GError **error)
{
  if (opt_verbose < 2)
    opt_verbose++;

  return TRUE;
}

static gboolean
opt_rotate_cb (const char *name,
               const char *value,
               gpointer data,
               GError **error)
{
  char *endptr;
  gint64 i64 = g_ascii_strtoll (value, &endptr, 10);
  goffset unit = _srt_byte_suffix_to_multiplier (endptr);

  if (i64 < 0 || endptr == value || unit == 0 || i64 >= (G_MAXINT64 / unit))
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                   "Invalid file size limit: %s", value);
      return FALSE;
    }

  opt_max_bytes = i64 * unit;
  return TRUE;
}

static const GOptionEntry option_entries[] =
{
  { "background", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_background,
    "Run the srt-logger process in the background, not as a child of "
    "the COMMAND or the parent process",
    NULL },
  { "exec-fallback", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_exec_fallback,
    "If unable to set up logging, run the wrapped command anyway",
    NULL },
  { "filename", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_filename,
    "Name for the log file in the --log-directory",
    "FILENAME" },
  { "identifier", 't',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &opt_identifier,
    "Identifier to use in the Journal and in default filenames",
    "STRING" },
  { "journal-fd", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &opt_journal_fd,
    "An open file descriptor pointing to the systemd Journal",
    "FD" },
  { "log-directory", 'd',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_log_directory,
    "Directory in which to write logs "
    "[default: $STEAM_CLIENT_LOG_FOLDER or ~/.steam/steam/logs]",
    "PATH" },
  { "log-fd", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &opt_log_fd,
    "An open file descriptor pointing to the --filename",
    "FD" },
  { "no-auto-terminal", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_auto_terminal,
    "Don't try to discover a terminal automatically.",
    NULL },
  { "rotate", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, &opt_rotate_cb,
    "Rotate log.txt to log.previous.txt after logging this many bytes, "
    "with optional MB, MiB (= M), kB or KiB (= K) suffix [default: 8M]",
    "BYTES" },
  { "sh-syntax", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_sh_syntax,
    "Print shell expressions ending with \"SRT_LOGGER_READY=1\\n\" on "
    "standard output when ready",
    NULL },
  { "terminal-fd", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &opt_terminal_fd,
    "An open file descriptor pointing to the terminal",
    "FD" },
  { "use-journal", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_use_journal,
    "Also log to the systemd Journal.", NULL },
  { "verbose", 'v',
    G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, &opt_verbose_cb,
    "Be more verbose. If given twice, log debug messages too.", NULL },
  { "version", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_version,
    "Print version number and exit.", NULL },
  { NULL }
};

static GSpawnError
spawn_error_from_errno (int saved_errno)
{
  switch (saved_errno)
    {
      /* This is the only one we actually treat differently */
      case ENOENT:
        return G_SPAWN_ERROR_NOENT;

      default:
        return G_SPAWN_ERROR_FAILED;
    }
}

static void
execvpe_wrapper (char **argv,
                 char **original_environ,
                 GError **error)
{
  int saved_errno;

  execvpe (argv[0], argv, original_environ);
  saved_errno = errno;
  /* If we are still here then execve failed */
  g_set_error (error,
               G_SPAWN_ERROR,
               spawn_error_from_errno (saved_errno),
               "Error replacing self with %s: %s",
               argv[0], g_strerror (saved_errno));
}

static gboolean
run (int argc,
     char **argv,
     GError **error)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(SrtLogger) logger = NULL;
  g_auto(GStrv) subproc_environ = NULL;
  g_autoptr(GOptionContext) option_context = NULL;
  glnx_autofd int original_stdout = -1;
  glnx_autofd int original_stderr = -1;
  gboolean consume_stdin;

  setlocale (LC_ALL, "");
  subproc_environ = g_get_environ ();
  _srt_setenv_disable_gio_modules ();

  if (!_srt_util_set_glib_log_handler (THIS_PROGRAM,
                                       G_LOG_DOMAIN, SRT_LOG_FLAGS_NONE,
                                       NULL, NULL, error))
    return FALSE;

  option_context = g_option_context_new ("[COMMAND [ARGUMENTS...]]");
  g_option_context_add_main_entries (option_context, option_entries, NULL);

  if (_srt_boolean_environment ("SRT_LOGGER_USE_JOURNAL", FALSE))
    opt_use_journal = TRUE;

  if (!g_option_context_parse (option_context, &argc, &argv, error))
    return FALSE;

  if (!_srt_boolean_environment ("SRT_LOG_ROTATION", TRUE))
    opt_max_bytes = 0;

  if (opt_version)
    {
      g_print (
          "%s:\n"
          " Package: steam-runtime-tools\n"
          " Version: %s\n",
          THIS_PROGRAM, VERSION);
      return TRUE;
    }

  if (!_srt_util_set_glib_log_handler (NULL, G_LOG_DOMAIN,
                                       (SRT_LOG_FLAGS_DIVERT_STDOUT
                                        | SRT_LOG_FLAGS_OPTIONALLY_JOURNAL
                                        | (opt_verbose >= 2 ? SRT_LOG_FLAGS_DEBUG : 0)
                                        | (opt_verbose ? SRT_LOG_FLAGS_INFO : 0)),
                                       &original_stdout, &original_stderr,
                                       error))
    return FALSE;

  _srt_unblock_signals ();

  /* Ignore SIGPIPE so that on error writing to any log sink, we continue
   * to try to write to the others (if any). If we are running a subprocess,
   * this is also inherited by the subprocess so that its log output writes
   * will not fail if srt-logger has crashed. */
  _srt_ignore_sigpipe ();

  if (argc >= 2 && strcmp (argv[1], "--") == 0)
    {
      argv++;
      argc--;
    }

  consume_stdin = (argc < 2);

  logger = _srt_logger_new_take (g_strdup (argv[1]),
                                 opt_background,
                                 g_steal_pointer (&opt_filename),
                                 g_steal_fd (&opt_log_fd),
                                 g_steal_pointer (&opt_identifier),
                                 opt_use_journal,
                                 g_steal_fd (&opt_journal_fd),
                                 g_steal_pointer (&opt_log_directory),
                                 opt_max_bytes,
                                 g_steal_fd (&original_stderr),
                                 opt_sh_syntax,
                                 opt_auto_terminal,
                                 opt_terminal_fd);

  if (opt_background || !consume_stdin)
    {
      const char *executable = NULL;

      if (_srt_find_myself (&executable, NULL, &local_error) != NULL
          && _srt_logger_run_subprocess (logger, executable, consume_stdin,
                                         _srt_const_strv (subproc_environ),
                                         &original_stdout,
                                         &local_error))
        {
          /* Add SRT_LOG_TERMINAL, SRT_LOG_TO_JOURNAL to the environment
           * to be used for the subprocess */
          subproc_environ = _srt_logger_modify_environ (logger, subproc_environ);
        }
      else
        {
          if (consume_stdin || !opt_exec_fallback)
            {
              g_propagate_error (error, g_steal_pointer (&local_error));
              return FALSE;
            }

          /* Fall through to the equivalent of: COMMAND >&2 */
          _srt_log_failure ("Unable to start logging: %s",
                            local_error->message);
          _srt_log_failure ("Falling back to just running the program");
        }

      glnx_close_fd (&original_stdout);

      if (consume_stdin)
        return TRUE;

      execvpe_wrapper (argv + 1, subproc_environ, error);
      return FALSE;
    }
  else
    {
      const char * const cat[] = { "cat", NULL };

      if (_srt_logger_process (logger, &original_stdout, &local_error))
        return TRUE;

      if (!opt_exec_fallback)
        {
          g_propagate_error (error, g_steal_pointer (&local_error));
          return FALSE;
        }

      /* Fall through to the equivalent of: cat >&2 */
      _srt_log_failure ("Unable to start logging: %s",
                        local_error->message);
      _srt_log_failure ("Falling back to the equivalent of cat >&2");

      glnx_close_fd (&original_stdout);
      execvpe_wrapper ((char **) cat, subproc_environ, error);
      return FALSE;
    }
}

int
main (int argc,
      char **argv)
{
  g_autoptr(GError) error = NULL;

  if (run (argc, argv, &error))
    return 0;

  if (error == NULL)
    g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED, "Assertion failure");

  _srt_log_failure ("%s", error->message);

  if (g_error_matches (error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT))
    return LAUNCH_EX_NOT_FOUND;
  else if (error->domain == G_SPAWN_ERROR)
    return LAUNCH_EX_CANNOT_INVOKE;
  else
    return LAUNCH_EX_FAILED;
}
