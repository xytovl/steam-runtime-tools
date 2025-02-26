/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "steam-runtime-tools/logger-internal.h"

#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>

#include <glib.h>
#include "libglnx.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/launcher-internal.h"
#include "steam-runtime-tools/log-internal.h"
#include "steam-runtime-tools/missing-internal.h"
#include "steam-runtime-tools/utils-internal.h"

static const char READY_MESSAGE[] = "SRT_LOGGER_READY=1\n";
#define READY_MESSAGE_LEN (sizeof (READY_MESSAGE) - 1)

typedef const char *const SyslogLevelNames[3];

static const SyslogLevelNames syslog_level_names[] = {
  [LOG_EMERG]   = { "emerg",   "emergency"     },
  [LOG_ALERT]   = { "alert"                    },
  [LOG_CRIT]    = { "crit",    "critical"      },
  [LOG_ERR]     = { "err",     "error",    "e" },
  [LOG_WARNING] = { "warning", "warn",     "w" },
  [LOG_NOTICE]  = { "notice",              "n" },
  [LOG_INFO]    = { "info",                "i" },
  [LOG_DEBUG]   = { "debug",               "d" }
};

/* Ensure there are no gaps. */
G_STATIC_ASSERT (G_N_ELEMENTS (syslog_level_names) == LOG_DEBUG + 1);

gboolean
_srt_syslog_level_parse (const char *s,
                         int *out_level,
                         GError **error)
{
  int level, name;

  if (g_ascii_isdigit (*s))
    {
      guint64 out;

      if (!g_ascii_string_to_unsigned (s, 10, 0, G_N_ELEMENTS (syslog_level_names) - 1, &out, error))
        return FALSE;

      *out_level = (int) out;
      return TRUE;
    }

  for (level = 0; level < G_N_ELEMENTS (syslog_level_names); level++)
    {
      const SyslogLevelNames *names = &syslog_level_names[level];

      for (name = 0;
           name < G_N_ELEMENTS (*names) && (*names)[name] != NULL;
           name++)
        {
          if (g_ascii_strcasecmp ((*names)[name], s) == 0)
            {
              *out_level = level;
              return TRUE;
            }
        }
    }

  g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
               "Not a recognised log level");
  return FALSE;
}

struct _SrtLogger {
  GObject parent;
  const char *prgname;
  gchar *argv0;
  gchar *identifier;
  gchar *filename;
  gchar *previous_filename;
  gchar *new_filename;
  gchar *log_dir;
  gchar *terminal;
  struct stat file_stat;
  int child_ready_to_parent;
  int pipe_from_parent;
  int original_stderr;
  int file_fd;
  int journal_fd;
  int terminal_fd;
  goffset max_bytes;
  int default_level;
  int file_level;
  int journal_level;
  int terminal_level;
  unsigned background : 1;
  unsigned sh_syntax : 1;
  unsigned timestamps : 1;
  unsigned use_file : 1;
  unsigned use_journal : 1;
  unsigned use_stderr : 1;
  unsigned use_terminal : 1;
  unsigned use_terminal_colors : 1;
  unsigned parse_level_prefix : 1;
};

struct _SrtLoggerClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE (SrtLogger, _srt_logger, G_TYPE_OBJECT)

static void
_srt_logger_init (SrtLogger *self)
{
  /* localtime_r() is documented to require this as initialization */
  tzset ();

  self->child_ready_to_parent = -1;
  self->pipe_from_parent = -1;
  self->original_stderr = -1;
  self->file_fd = -1;
  self->journal_fd = -1;
  self->terminal_fd = -1;
  self->max_bytes = -1;
  self->default_level = SRT_SYSLOG_LEVEL_DEFAULT_LINE;
  self->file_level = SRT_SYSLOG_LEVEL_DEFAULT_FILE;
  self->journal_level = SRT_SYSLOG_LEVEL_DEFAULT_JOURNAL;
  self->terminal_level = SRT_SYSLOG_LEVEL_DEFAULT_TERMINAL;
  self->timestamps = TRUE;
}

/* We need to have the log open read/write, otherwise the kernel won't let
 * us take out a shared (read) lock. */
#define OPEN_FLAGS \
  (O_APPEND | O_CLOEXEC | O_CREAT | O_NOCTTY | O_RDWR)

#define EXCLUSIVE_LOCK { \
  .l_type = F_WRLCK, \
  .l_whence = SEEK_SET, \
  .l_start = 0, \
  .l_len = 0, \
}
#define SHARED_LOCK { \
  .l_type = F_RDLCK, \
  .l_whence = SEEK_SET, \
  .l_start = 0, \
  .l_len = 0, \
}

static void
_srt_logger_finalize (GObject *object)
{
  SrtLogger *self = SRT_LOGGER (object);

  g_clear_pointer (&self->argv0, g_free);
  g_clear_pointer (&self->log_dir, g_free);
  g_clear_pointer (&self->identifier, g_free);
  g_clear_pointer (&self->filename, g_free);
  g_clear_pointer (&self->previous_filename, g_free);
  g_clear_pointer (&self->new_filename, g_free);
  g_clear_pointer (&self->terminal, g_free);
  glnx_close_fd (&self->child_ready_to_parent);
  glnx_close_fd (&self->pipe_from_parent);
  glnx_close_fd (&self->file_fd);

  if (self->journal_fd > STDERR_FILENO)
    glnx_close_fd (&self->journal_fd);

  if (self->terminal_fd > STDERR_FILENO)
    glnx_close_fd (&self->terminal_fd);

  G_OBJECT_CLASS (_srt_logger_parent_class)->finalize (object);
}

static void
_srt_logger_class_init (SrtLoggerClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->finalize = _srt_logger_finalize;
}

/*
 * _srt_logger_new_take:
 * @argv0: (transfer full): Command that is being logged, or %NULL
 * @background: If true, run srt-logger in the background
 * @filename: (transfer full): Basename of log file, or %NULL
 * @file_fd: Existing file descriptor pointing to @filename, or -1
 * @identifier: (transfer full): syslog identifier that is being logged, or %NULL
 * @journal: If true, log to the systemd Journal even if no @journal_fd
 * @journal_fd: Existing file descriptor from sd_journal_stream_fd(), or -1
 * @log_dir: (transfer full): Directory for logs, or %NULL
 * @max_bytes: Rotate file-based log when it would exceed this
 * @original_stderr: Original stderr before setting up logging, or -1
 * @sh_syntax: If true, write `SRT_LOGGER_READY=1\n` to stdout when ready
 * @terminal: If true, try to log to the terminal
 * @terminal_fd: Existing file descriptor for the terminal, or -1
 * @timestamps: If true, prepend a timestamp to each line
 *
 * Create a new logger. All parameters are "stolen".
 */
SrtLogger *
_srt_logger_new_take (char *argv0,
                      gboolean background,
                      int default_line_level,
                      char *filename,
                      int file_fd,
                      int file_level,
                      char *identifier,
                      gboolean journal,
                      int journal_fd,
                      int journal_level,
                      char *log_dir,
                      goffset max_bytes,
                      int original_stderr,
                      gboolean parse_level_prefix,
                      gboolean sh_syntax,
                      gboolean terminal,
                      int terminal_fd,
                      int terminal_level,
                      gboolean timestamps)
{
  SrtLogger *self = g_object_new (SRT_TYPE_LOGGER, NULL);

  self->argv0 = argv0;
  self->background = !!background;
  self->default_level = default_line_level;
  self->filename = filename;
  self->file_fd = file_fd;
  self->file_level = file_level;
  self->identifier = identifier;
  self->journal_fd = journal_fd;
  self->journal_level = journal_level;
  self->log_dir = log_dir;
  self->max_bytes = max_bytes;
  self->use_file = TRUE;
  self->use_journal = !!journal;
  self->original_stderr = original_stderr;
  self->parse_level_prefix = !!parse_level_prefix;
  self->sh_syntax = !!sh_syntax;
  self->use_terminal = !!terminal;
  self->terminal_fd = terminal_fd;
  self->terminal_level = terminal_level;
  self->timestamps = !!timestamps;
  return g_steal_pointer (&self);
}

static gboolean
_srt_logger_setup (SrtLogger *self,
                   GError **error)
{
  gboolean stderr_is_journal;
  gboolean redirecting = FALSE;

  g_return_val_if_fail (self->prgname == NULL, FALSE);
  g_return_val_if_fail (self->previous_filename == NULL, FALSE);
  g_return_val_if_fail (self->new_filename == NULL, FALSE);
  g_return_val_if_fail (self->pipe_from_parent < 0, FALSE);
  g_return_val_if_fail (self->terminal == NULL, FALSE);

  self->use_stderr = FALSE;

  if (self->identifier == NULL
      && self->filename == NULL
      && self->argv0 != NULL)
    {
      g_debug ("identifier defaults to argv[0]: %s", self->argv0);
      self->identifier = g_strdup (self->argv0);
    }

  if (self->identifier == NULL
      && self->filename != NULL
      && self->filename[0] != '\0')
    {
      char *dot;

      self->identifier = g_strdup (self->filename);

      dot = strrchr (self->identifier, '.');

      if (dot != NULL && dot > self->identifier)
        *dot = '\0';

      g_debug ("identifier defaults to (part of) filename: %s", self->identifier);
    }

  if (self->identifier != NULL
      && self->identifier[0] != '\0'
      && self->filename == NULL)
    {
      g_debug ("filename defaults to identifier %s + .txt", self->identifier);
      self->filename = g_strconcat (self->identifier, ".txt", NULL);
    }

  if (self->use_file && self->filename != NULL && self->filename[0] != '\0')
    {
      const char *dot;

      g_debug ("Logging to file: %s", self->filename);

      if (strchr (self->filename, '/') != NULL)
        return glnx_throw (error, "Invalid filename \"%s\": should not contain '/'",
                           self->filename);

      if (self->filename[0] == '.')
        return glnx_throw (error, "Invalid filename \"%s\": should not start with '.'",
                           self->filename);

      if (strlen (self->filename) > INT_MAX)
        return glnx_throw (error, "Invalid filename \"%s\": ludicrously long",
                           self->filename);

      dot = strrchr (self->filename, '.');

      if (dot == NULL)
        {
          self->previous_filename = g_strconcat (self->filename, ".previous", NULL);
          self->new_filename = g_strdup_printf (".%s.new", self->filename);
        }
      else
        {
          self->previous_filename = g_strdup_printf ("%.*s.previous%s",
                                                     (int) (dot - self->filename),
                                                     self->filename,
                                                     dot);
          self->new_filename = g_strdup_printf (".%.*s.new%s",
                                                (int) (dot - self->filename),
                                                self->filename,
                                                dot);
        }
    }
  else
    {
      self->use_file = FALSE;
      g_clear_pointer (&self->filename, g_free);
    }

  /* Automatically use the Journal if stderr is the Journal */
  stderr_is_journal = g_log_writer_is_journald (STDERR_FILENO);

  if (stderr_is_journal)
    {
      g_debug ("logging to Journal because stderr is the Journal");
      self->use_journal = TRUE;
    }

  if (self->journal_fd >= 0)
    {
      int result;

      g_debug ("logging to existing Journal stream");
      self->use_journal = TRUE;

      /* We never want to mark stdin/stdout/stderr as close-on-exec */
      if (self->journal_fd > STDERR_FILENO)
        result = _srt_fd_set_close_on_exec (self->journal_fd);
      else
        result = _srt_fd_unset_close_on_exec (self->journal_fd);

      if (result < 0)
        return glnx_throw_errno_prefix (error,
                                        "Unable to accept journal fd %d",
                                        self->journal_fd);
    }
  else if (self->identifier != NULL
           && self->identifier[0] != '\0'
           && self->use_journal)
    {
      /* Open the Journal stream here, to get everything logged with the
       * process ID of the command whose output we want to log */
      g_autoptr(GError) journal_error = NULL;

      g_debug ("opening new Journal stream: %s", self->identifier);
      self->journal_fd = _srt_journal_stream_fd (self->identifier,
                                                 LOG_INFO,
                                                 TRUE,
                                                 &journal_error);

      if (self->journal_fd < 0)
        {
          g_debug ("Unable to connect to systemd Journal: %s",
                   journal_error->message);
          g_clear_error (&journal_error);

          /* If stderr was already a journald stream, we might as well
           * keep using it */
          if (stderr_is_journal)
            self->journal_fd = STDERR_FILENO;
          else
            self->use_journal = FALSE;
        }
      else
        {
          redirecting = TRUE;
        }
    }
  else if (stderr_is_journal)
    {
      /* Even if self->identifier is empty, we can keep using a pre-existing
       * journald stream inherited from our parent */
      g_assert (self->use_journal);
      self->journal_fd = STDERR_FILENO;
    }


  if (self->log_dir == NULL && self->use_file)
    {
      const char *dir = g_getenv ("SRT_LOG_DIR");

      if (dir != NULL)
        {
          self->log_dir = g_strdup (dir);
          g_debug ("using $SRT_LOG_DIR: %s", dir);
        }
      else
        {
          const char *source = NULL;

          dir = g_getenv ("STEAM_CLIENT_LOG_FOLDER");
          source = "$STEAM_CLIENT_LOG_FOLDER";

          if (dir == NULL)
            {
              dir = "logs";
              source = "default log directory";
            }

          self->log_dir = g_build_filename (g_get_home_dir (),
                                            ".steam", "steam", dir,
                                            NULL);

          g_debug ("using %s: %s", source, self->log_dir);
        }
    }

  if (self->use_file
      && !g_file_test (self->log_dir, G_FILE_TEST_IS_DIR))
    return glnx_throw (error, "\"%s\" is not a directory",
                       self->log_dir);

  if (self->file_fd >= 0)
    {
      g_debug ("logging to existing file stream");
      self->use_file = TRUE;

      if (_srt_fd_set_close_on_exec (self->file_fd) < 0)
        return glnx_throw_errno_prefix (error,
                                        "Unable to accept log fd %d",
                                        self->file_fd);

      if (self->filename == NULL)
        return glnx_throw (error,
                           "Providing a log fd requires a filename");
      if (fstat (self->file_fd, &self->file_stat) < 0)
        return glnx_throw_errno_prefix (error,
                                        "Unable to stat \"%s\"",
                                        self->filename);
    }
  else if (self->use_file)
    {
      g_autoptr(GDateTime) date_time = NULL;
      g_autoptr(GString) message = NULL;
      g_autofree gchar *timestamp = NULL;
      glnx_autofd int dir_fd = -1;

      g_assert (self->log_dir != NULL);
      g_assert (self->filename != NULL);
      g_debug ("logging to new file: %s", self->filename);

      redirecting = TRUE;

      if (!glnx_opendirat (AT_FDCWD, self->log_dir, TRUE, &dir_fd, error))
        return FALSE;

      self->file_fd = TEMP_FAILURE_RETRY (openat (dir_fd, self->filename,
                                                  OPEN_FLAGS,
                                                  0644));

      if (self->file_fd < 0)
        return glnx_throw_errno_prefix (error,
                                        "Unable to open \"%s\"",
                                        self->filename);

      if (fstat (self->file_fd, &self->file_stat) < 0)
        return glnx_throw_errno_prefix (error,
                                        "Unable to stat \"%s\"",
                                        self->filename);

      /* As a special case, the message saying that we opened the log file
       * always has a timestamp, even if timestamps are disabled in general */
      message = g_string_new ("");
      date_time = g_date_time_new_now_local ();
      /* We record the time zone here, but not in subsequent lines:
       * the reader can infer that subsequent lines are in the same
       * time zone as this message. */
      timestamp = g_date_time_format (date_time, "%F %T%z");
      g_string_append_printf (message, "[%s] %s[%d]: Log opened\n",
                              timestamp, g_get_prgname (), getpid ());
      glnx_loop_write (self->file_fd, message->str, message->len);
    }

  if (self->terminal_fd >= 0)
    {
      g_debug ("logging to existing terminal fd");
      self->use_terminal = TRUE;
    }
  else if (self->use_terminal)
    {
      const char *terminal = g_getenv ("SRT_LOG_TERMINAL");

      if (terminal != NULL && terminal[0] != '\0')
        {
          g_debug ("trying to log to terminal %s", terminal);
          self->terminal_fd = open (terminal,
                                    O_CLOEXEC | O_APPEND | O_WRONLY);

          if (self->terminal_fd < 0)
            {
              _srt_log_warning ("Unable to open terminal \"%s\"", terminal);
              self->use_terminal = FALSE;
            }
        }
      else if (terminal != NULL)
        {
          g_debug ("automatic use of terminal disabled by SRT_LOG_TERMINAL=''");
        }
      else if (isatty (STDERR_FILENO))
        {
          self->terminal_fd = STDERR_FILENO;
        }
      else if (isatty (self->original_stderr))
        {
          self->terminal_fd = g_steal_fd (&self->original_stderr);
        }
      else
        {
          g_debug ("unable to find a terminal file descriptor");
          self->use_terminal = FALSE;
        }
    }

  if (self->use_terminal)
    {
      const char *no_color = g_getenv ("NO_COLOR");
      if (no_color == NULL || g_str_equal (no_color, ""))
        self->use_terminal_colors = TRUE;
    }

  if (self->terminal_fd >= 0 && self->terminal == NULL)
    {
      char buf[32];

      if (ttyname_r (self->terminal_fd, buf, sizeof (buf)) == 0)
        self->terminal = g_strdup (buf);
    }

  if (self->file_fd < 0
      && self->journal_fd < 0
      && (self->terminal_fd < 0
          || !_srt_fstatat_is_same_file (self->terminal_fd, "",
                                         STDERR_FILENO, "")))
    {
      /* No file, no Journal, and either no terminal or the terminal
       * is elsewhere */
      g_debug ("Continuing to write to stderr");
      self->use_stderr = TRUE;
    }
  else if (stderr_is_journal
           && self->journal_fd >= 0
           && self->file_fd < 0
           && self->terminal_fd < 0)
    {
      /* We were only writing to the Journal, and we are still writing
       * to the Journal; nothing has changed, so don't make a lot of noise */
      g_debug ("Continuing to write to Journal");
    }
  else if (redirecting)
    {
      g_autoptr(GPtrArray) sinks = g_ptr_array_new_full (3, g_free);
      g_autoptr(GString) message = g_string_new ("");
      gsize i;

      if (self->file_fd >= 0)
        g_ptr_array_add (sinks,
                         g_strdup_printf ("file \"%s/%s\"",
                                          self->log_dir, self->filename));

      if (self->journal_fd >= 0)
        {
          if (self->identifier != NULL)
            g_ptr_array_add (sinks,
                             g_strdup_printf ("systemd Journal (as \"%s\")",
                                              self->identifier));
          else
            g_ptr_array_add (sinks, g_strdup ("systemd Journal"));
        }

      if (self->terminal_fd >= 0)
        {
          if (self->terminal != NULL)
            g_ptr_array_add (sinks,
                             g_strdup_printf ("terminal \"%s\"", self->terminal));
          else
            g_ptr_array_add (sinks, g_strdup ("terminal"));
        }

      for (i = 0; i < sinks->len; i++)
        {
          g_string_append (message, g_ptr_array_index (sinks, i));

          if (i + 2 == sinks->len)
            g_string_append (message, " and ");
          else if (i + 1 != sinks->len)
            g_string_append (message, ", ");
        }

      g_assert (message->len > 0);
      g_info ("Sending log messages to %s", message->str);
    }
  else
    {
      g_debug ("Logging to fds provided by parent");
    }

  return TRUE;
}

static void
logger_child_setup_cb (void *user_data)
{
  SrtLogger *self = user_data;

  g_fdwalk_set_cloexec (3);

  if (self->background)
    {
      pid_t pid;

      pid = setsid ();

      if (pid == (pid_t) -1)
        _srt_async_signal_safe_error (self->prgname,
                                      "Unable to create new session",
                                      LAUNCH_EX_FAILED);

      pid = fork ();

      if (pid == (pid_t) -1)
        _srt_async_signal_safe_error (self->prgname,
                                      "Unable to create daemonized process",
                                      LAUNCH_EX_FAILED);

      if (pid != 0)
        {
          /* Intermediate process exits, causing the child to be
           * reparented to init.
           * Our parent reads from the pipe child_ready_to_parent
           * to know when the child is ready, and whether it was
           * successful. */
          _exit (0);
        }
    }

  if (self->pipe_from_parent >= 0 &&
      dup2 (self->pipe_from_parent, STDIN_FILENO) != STDIN_FILENO)
    _srt_async_signal_safe_error (self->prgname,
                                  "Unable to assign file descriptor",
                                  LAUNCH_EX_FAILED);

  if (dup2 (self->child_ready_to_parent, STDOUT_FILENO) != STDOUT_FILENO)
    _srt_async_signal_safe_error (self->prgname,
                                  "Unable to assign file descriptor",
                                  LAUNCH_EX_FAILED);

  if (self->file_fd >= 0 && _srt_fd_unset_close_on_exec (self->file_fd) < 0)
    _srt_async_signal_safe_error (self->prgname,
                                  "Unable to make log file fd inheritable",
                                  LAUNCH_EX_FAILED);

  if (self->journal_fd >= 0 && _srt_fd_unset_close_on_exec (self->journal_fd) < 0)
    _srt_async_signal_safe_error (self->prgname,
                                  "Unable to make journal stream inheritable",
                                  LAUNCH_EX_FAILED);

  if (self->terminal_fd >= 0 && _srt_fd_unset_close_on_exec (self->terminal_fd) < 0)
    _srt_async_signal_safe_error (self->prgname,
                                  "Unable to make terminal fd inheritable",
                                  LAUNCH_EX_FAILED);
}

static void
show_daemonized_logger_pid (GString *status)
{
  size_t i;

  for (i = 0; i < status->len; i++)
    {
      if ((i == 0 || status->str[i - 1] == '\n')
          && g_str_has_prefix (status->str + i, "SRT_LOGGER_PID="))
        {
          /* Temporarily \0-terminate the line; we'll put back the
           * newline afterwards */
          char *newline = strchr (status->str + i, '\n');

          if (newline != NULL)
            *newline = '\0';

          g_debug ("Background logger subprocess is process %s",
                   status->str + i + strlen ("SRT_LOGGER_PID="));

          if (newline != NULL)
            *newline = '\n';

          break;
        }
    }
}

/*
 * _srt_logger_run_subprocess:
 * @self: Parameters for how to carry out logging
 * @logger: Path to the srt-logger executable
 * @envp: Environment variables
 * @original_stdout: (inout): Pointer to a fd that is the current process's
 *  original standard output
 * @error: Error indicator, see GLib documentation
 *
 * Attempt to run a subprocess capturing the current process's standard
 * output and standard error and writing them to log destinations.
 */
gboolean
_srt_logger_run_subprocess (SrtLogger *self,
                            const char *logger,
                            gboolean consume_stdin,
                            const char * const *envp,
                            int *original_stdout,
                            GError **error)
{
  g_autoptr(GPtrArray) logger_argv = NULL;
  g_autoptr(GString) status = NULL;
  g_auto(SrtPipe) child_pipe = _SRT_PIPE_INIT;
  g_auto(SrtPipe) ready_pipe = _SRT_PIPE_INIT;
  GPid pid;
  int i;

  g_return_val_if_fail (self->child_ready_to_parent < 0, FALSE);
  g_return_val_if_fail (self->pipe_from_parent < 0, FALSE);

  if (!_srt_logger_setup (self, error))
    return FALSE;

  if (!consume_stdin && !_srt_pipe_open (&child_pipe, error))
    return FALSE;

  if (!_srt_pipe_open (&ready_pipe, error))
    return FALSE;

  logger_argv = g_ptr_array_new_with_free_func (g_free);
  self->prgname = g_get_prgname ();
  self->pipe_from_parent = _srt_pipe_steal (&child_pipe, _SRT_PIPE_END_READ);
  self->child_ready_to_parent = _srt_pipe_steal (&ready_pipe, _SRT_PIPE_END_WRITE);

  g_ptr_array_add (logger_argv, g_strdup (logger));
  g_ptr_array_add (logger_argv, g_strdup ("--sh-syntax"));

  if (self->max_bytes > 0 && _srt_boolean_environment ("SRT_LOG_ROTATION", TRUE))
    g_ptr_array_add (logger_argv,
                     g_strdup_printf ("--rotate=%" G_GOFFSET_FORMAT,
                                      self->max_bytes));

  if (self->file_fd >= 0)
    {
      g_debug ("Passing file fd %d to logging subprocess", self->file_fd);
      g_assert (self->filename != NULL);
      g_assert (self->log_dir != NULL);
      g_ptr_array_add (logger_argv, g_strdup ("--log-directory"));
      g_ptr_array_add (logger_argv, g_strdup (self->log_dir));
      g_ptr_array_add (logger_argv, g_strdup ("--filename"));
      g_ptr_array_add (logger_argv, g_strdup (self->filename));
      g_ptr_array_add (logger_argv,
                       g_strdup_printf ("--log-fd=%d", self->file_fd));
    }

  if (self->journal_fd >= 0)
    {
      g_debug ("Passing Journal fd %d to logging subprocess", self->journal_fd);
      g_ptr_array_add (logger_argv,
                       g_strdup_printf ("--journal-fd=%d", self->journal_fd));
    }

  if (self->terminal_fd >= 0)
    {
      g_debug ("Passing terminal fd %d to logging subprocess", self->terminal_fd);
      g_ptr_array_add (logger_argv,
                       g_strdup_printf ("--terminal-fd=%d", self->terminal_fd));
    }

  if (!self->timestamps)
    g_ptr_array_add (logger_argv, g_strdup ("--no-timestamps"));

  if (self->parse_level_prefix)
    g_ptr_array_add (logger_argv, g_strdup ("--parse-level-prefix"));

  if (self->default_level != SRT_SYSLOG_LEVEL_DEFAULT_LINE)
    g_ptr_array_add (logger_argv,
                     g_strdup_printf ("--default-level=%s",
                                      syslog_level_names[self->default_level][0]));

  if (self->file_level != SRT_SYSLOG_LEVEL_DEFAULT_FILE)
    g_ptr_array_add (logger_argv,
                     g_strdup_printf ("--file-level=%s",
                                      syslog_level_names[self->file_level][0]));
  if (self->journal_level != SRT_SYSLOG_LEVEL_DEFAULT_JOURNAL)
    g_ptr_array_add (logger_argv,
                     g_strdup_printf ("--journal-level=%s",
                                      syslog_level_names[self->journal_level][0]));
  if (self->terminal_level != SRT_SYSLOG_LEVEL_DEFAULT_TERMINAL)
    g_ptr_array_add (logger_argv,
                     g_strdup_printf ("--terminal-level=%s",
                                      syslog_level_names[self->terminal_level][0]));

  if (_srt_util_is_verbose ())
    g_ptr_array_add (logger_argv, g_strdup ("-v"));

  if (_srt_util_is_debugging ())
    g_ptr_array_add (logger_argv, g_strdup ("-v"));

  g_ptr_array_add (logger_argv, NULL);

  GSpawnFlags spawn_flags;

  spawn_flags = (G_SPAWN_SEARCH_PATH
                 | G_SPAWN_DO_NOT_REAP_CHILD
                 | G_SPAWN_LEAVE_DESCRIPTORS_OPEN);

  if (consume_stdin)
    spawn_flags |= G_SPAWN_CHILD_INHERITS_STDIN;

  if (!g_spawn_async (self->log_dir,
                      (char **) logger_argv->pdata,
                      (char **) envp,
                      spawn_flags,
                      logger_child_setup_cb, self,
                      &pid,
                      error))
    return FALSE;

  if (self->background)
    {
      pid_t result;
      int wstatus;

      g_debug ("Opened daemonized logger subprocess");
      /* Reap the intermediate process, allowing the daemonized logger
       * subprocess to be reparented to init or the nearest subreaper */
      result = TEMP_FAILURE_RETRY (waitpid (pid, &wstatus, 0));

      if (result == (pid_t) -1)
        return glnx_throw_errno_prefix (error,
                                        "Unable to wait for intermediate child process %" G_PID_FORMAT,
                                        pid);

      g_assert (result == pid);
    }
  else
    {
      g_debug ("Opened logger subprocess %" G_PID_FORMAT ", will redirect output to it",
               pid);
    }

  /* These are only needed in the child */
  glnx_close_fd (&self->child_ready_to_parent);
  glnx_close_fd (&self->pipe_from_parent);

  /* Wait for child to finish setup */
  status = g_string_new ("");

  if (!_srt_string_read_fd_until_eof (status, ready_pipe.fds[_SRT_PIPE_END_READ], error))
    return glnx_prefix_error (error, "Unable to read status from srt-logger subprocess");

  if (strlen (status->str) != status->len)
    return glnx_throw (error, "Status from srt-logger subprocess contains \\0");

  if (self->background && _srt_util_is_debugging ())
    show_daemonized_logger_pid (status);

  if (!_srt_string_ends_with (status, READY_MESSAGE))
    return glnx_throw (error, "Unable to parse status from srt-logger subprocess: %s", status->str);

  if (self->sh_syntax
      && glnx_loop_write (*original_stdout, status->str, status->len) < 0)
    return glnx_throw_errno_prefix (error, "Unable to report ready");

  glnx_close_fd (original_stdout);

  if (!consume_stdin)
    {
      for (i = STDOUT_FILENO; i <= STDERR_FILENO; i++)
        {
          if (dup2 (child_pipe.fds[_SRT_PIPE_END_WRITE], i) != i)
            return glnx_throw_errno_prefix (error,
                                            "Unable to make fd %d a copy of %d",
                                            i,
                                            child_pipe.fds[_SRT_PIPE_END_WRITE]);
        }
    }

  return TRUE;
}

static gboolean
lock_output_file (const char *filename,
                  int fd,
                  GError **error)
{
  struct flock shared_lock = SHARED_LOCK;

  g_return_val_if_fail (filename != NULL, FALSE);
  g_return_val_if_fail (fd >= 0, FALSE);

  /* Fall back from OFD locking to legacy POSIX locking if necessary:
   * in this case we will not rotate logs */
  if (TEMP_FAILURE_RETRY (fcntl (fd, F_OFD_SETLKW, &shared_lock)) != 0
      && (errno != EINVAL
          || TEMP_FAILURE_RETRY (fcntl (fd, F_SETLKW, &shared_lock)) != 0))
    return glnx_throw_errno_prefix (error, "Unable to take shared lock on %s",
                                    filename);

  return TRUE;
}

/*
 * @self: The logger
 * @buf: Pointer to the beginning of a partial log message to parse
 * @len: Number of bytes to process, including the trailing newline if this is
 * a complete line
 * @out_level: (out): Pointer to the target of the parsed level
 *
 * Attempt to parse a log level prefix (`<N>`) or directive
 * (`<remaining-lines-assume-level=N>`) from the given line. If the presence of
 * a prefix cannot be determined because not enough data is available, return
 * -1 and leave @out_level all as-is. Otherwise, return the length of the parsed
 * prefix to be stripped off of the line (0 if there was no prefix), set
 * @out_level to contain the log level to use for the line. If the entire input
 * was consumed, then the return value will be equal to @len.
 *
 * Returns: the length of the parsed prefix, or -1 if not enough data is
 * available
 * */
static ssize_t
logger_parse_level_prefix (SrtLogger *self,
                           const char *buf,
                           size_t len,
                           int *out_level)
{
  static const char remaining_lines_prefix[] = "remaining-lines-assume-level=";

  const char *buf_start = buf;
  gboolean stop_parsing_prefix = FALSE;
  int level = LOG_DEBUG;

  if (!self->parse_level_prefix)
    {
      *out_level = self->default_level;
      return 0;
    }

  if (len == 0)
    return -1;

  if (*buf++ != '<')
    goto unprefixed;
  len--;

  if (strncmp (buf, remaining_lines_prefix,
               MIN (strlen (remaining_lines_prefix), len)) == 0)
    {
      if (len < strlen (remaining_lines_prefix))
        return -1;

      stop_parsing_prefix = TRUE;
      buf += strlen (remaining_lines_prefix);
      len -= strlen (remaining_lines_prefix);
    }

  if (len == 0)
    return -1;
  if (!g_ascii_isdigit (*buf))
    goto unprefixed;
  level = *buf++ - '0';
  len--;

  if (level >= G_N_ELEMENTS (syslog_level_names))
    goto unprefixed;

  if (len == 0)
    return -1;
  if (*buf++ != '>')
    goto unprefixed;
  len--;

  if (stop_parsing_prefix)
    {
      if (len == 0)
        return -1;
      else if (*buf != '\n')
        goto unprefixed;
      buf++;
      len--;

      self->default_level = level;
      self->parse_level_prefix = FALSE;
    }

  *out_level = level;
  return buf - buf_start;

unprefixed:
  *out_level = self->default_level;
  return 0;
}

/*
 * _srt_logger_try_rotate:
 * @self: The logger
 * @error: Error indicator, see GLib documentation
 *
 * Try to rotate a flat-file-based log: rename the #SrtLogger.filename to
 * #SrtLogger.previous_filename, and create a new #SrtLogger.filename.
 *
 * To avoid loss of information in error situations, if two processes
 * both have the same log open, then neither of them will rotate it.
 *
 * Returns: %TRUE if the log was rotated successfully
 */
static gboolean
_srt_logger_try_rotate (SrtLogger *self,
                        GError **error)
{
  struct flock exclusive_lock = EXCLUSIVE_LOCK;
  struct flock shared_lock = SHARED_LOCK;
  glnx_autofd int new_fd = -1;
  struct stat new_stat;
  gboolean ret = FALSE;

  g_debug ("Trying to rotate log file %s", self->filename);

  g_return_val_if_fail (self->filename != NULL, FALSE);
  g_return_val_if_fail (self->previous_filename != NULL, FALSE);
  g_return_val_if_fail (self->new_filename != NULL, FALSE);

  if (TEMP_FAILURE_RETRY (fcntl (self->file_fd,
                                 F_OFD_SETLK,
                                 &exclusive_lock)) != 0)
    return glnx_throw_errno_prefix (error, "Unable to take exclusive lock on %s",
                                    self->filename);

  if (TEMP_FAILURE_RETRY (unlink (self->previous_filename)) != 0
      && errno != ENOENT)
    return glnx_throw_errno_prefix (error, "Unable to remove previous filename %s",
                                    self->previous_filename);

  /* We create a hard link so that, if a concurrent process tries to open
   * the canonical filename, we will still have an exclusive lock on it. */
  if (TEMP_FAILURE_RETRY (link (self->filename, self->previous_filename)) != 0)
    {
      glnx_throw_errno_prefix (error, "Unable to hard-link %s as %s",
                               self->filename, self->previous_filename);
      goto out;
    }

  /* Open the new filename O_EXCL, so that if a concurrent process is
   * trying to do the same thing, we will just not open it. */
  new_fd = TEMP_FAILURE_RETRY (open (self->new_filename,
                                     OPEN_FLAGS | O_EXCL,
                                     0644));

  if (new_fd < 0)
    {
      glnx_throw_errno_prefix (error, "Unable to open new log file %s",
                               self->new_filename);
      goto out;
    }

  if (TEMP_FAILURE_RETRY (fcntl (new_fd, F_OFD_SETLK, &exclusive_lock)) != 0)
    {
      glnx_throw_errno_prefix (error,
                               "Unable to take exclusive lock on new log file %s",
                               self->new_filename);
      goto out;
    }

  if (fstat (new_fd, &new_stat) < 0)
    {
      glnx_throw_errno_prefix (error,
                               "Unable to stat \"%s\"",
                               self->new_filename);
      goto out;
    }

  if (TEMP_FAILURE_RETRY (rename (self->new_filename, self->filename)) != 0)
    {
      glnx_throw_errno_prefix (error, "Unable to rename %s to %s",
                               self->new_filename, self->filename);
      goto out;
    }

  glnx_close_fd (&self->file_fd);
  self->file_fd = g_steal_fd (&new_fd);
  self->file_stat = new_stat;
  ret = TRUE;

out:
  if (new_fd >= 0)
    {
      if (TEMP_FAILURE_RETRY (unlink (self->new_filename)) != 0)
        g_debug ("Unable to remove temporary new filename %s",
                 self->new_filename);
    }

  if (TEMP_FAILURE_RETRY (fcntl (self->file_fd, F_OFD_SETLK, &shared_lock)) != 0)
    g_debug ("Unable to return to a shared lock on new %s",
             self->filename);

  return ret;
}

static void
write_formatted_line (int fd,
                      int level,
                      const char *line,
                      size_t len)
{
  static const char ansi_reset[] = "\033[0m";
  static const char ansi_dim[] = "\033[2m";
  static const char ansi_bold[] = "\033[1m";
  static const char ansi_bold_magenta[] = "\033[1;35m";
  static const char ansi_bold_red[] = "\033[1;31m";

  glnx_loop_write (fd, ansi_reset, strlen (ansi_reset));

  switch (level) {
    case LOG_DEBUG:
      glnx_loop_write (fd, ansi_dim, strlen (ansi_dim));
      break;
    case LOG_INFO:
      break;
    case LOG_NOTICE:
      glnx_loop_write (fd, ansi_bold, strlen (ansi_bold));
      break;
    case LOG_WARNING:
      glnx_loop_write (fd, ansi_bold_magenta, strlen (ansi_bold_magenta));
      break;
    case LOG_ERR:
    case LOG_CRIT:
    case LOG_ALERT:
    case LOG_EMERG:
      glnx_loop_write (fd, ansi_bold_red, strlen (ansi_bold_red));
      break;
    default:
      g_warning ("Unexpected log level: %d", level);
      break;
  }

  if (len > 0 && line[len - 1] == '\n')
    {
      glnx_loop_write (fd, line, len - 1);
      glnx_loop_write (fd, ansi_reset, strlen (ansi_reset));
      glnx_loop_write (fd, "\n", 1);
    }
  else
    {
      glnx_loop_write (fd, line, len);
      glnx_loop_write (fd, ansi_reset, strlen (ansi_reset));
    }
}

/*
 * @self: The logger
 * @line: (array length=len): Pointer to the beginning or middle of
 *  a log message
 * @len: Number of bytes to process
 *
 * Send @len bytes of @line to destinations that expect to receive partial
 * log lines: a terminal (if used), and standard error (if we have no other
 * suitable destination).
 */
static void
logger_process_partial_line (SrtLogger *self,
                             int level,
                             const char *line,
                             size_t len)
{
  if (self->use_stderr && level <= self->terminal_level)
    glnx_loop_write (STDERR_FILENO, line, len);

  if (self->terminal_fd >= 0 && level <= self->terminal_level)
    {
      if (self->use_terminal_colors)
        write_formatted_line (self->terminal_fd, level, line, len);
      else
        glnx_loop_write (self->terminal_fd, line, len);
    }
}

/*
 * @self: The logger
 * @level: Severity level between `LOG_EMERG` and `LOG_DEBUG`
 * @line_start_time: If we are writing timestamps, the time at which the
 *  first byte of the @line was received
 * @line: (array length=len): Pointer to the beginning of a log message
 * @len: Number of bytes to process, including the trailing newline
 *
 * Send @len bytes of @line to destinations that expect to receive complete
 * log lines: the systemd Journal (if used), and a rotating
 * log file (if used).
 */
static void
logger_process_complete_line (SrtLogger *self,
                              int level,
                              time_t line_start_time,
                              const char *line,
                              size_t len)
{
  if (self->journal_fd >= 0 && level <= self->journal_level)
    {
      char prefix[] = {'<', '0', '>'};
      prefix[1] += level;
      glnx_loop_write (self->journal_fd, prefix, sizeof (prefix));
      glnx_loop_write (self->journal_fd, line, len);
    }

  if (self->file_fd >= 0 && level <= self->file_level)
    {
      if (self->filename != NULL)
        {
          const char *reason_to_reopen = NULL;
          struct stat current_stat;

          if (TEMP_FAILURE_RETRY (stat (self->filename, &current_stat)) == 0)
            {
              if (!_srt_is_same_stat (&current_stat, &self->file_stat))
                reason_to_reopen = "File replaced";
            }
          else
            {
              int saved_errno = errno;

              if (saved_errno != ENOENT)
                _srt_log_warning ("Unable to stat log file \"%s\": %s",
                                  self->filename, g_strerror (saved_errno));

              reason_to_reopen = g_strerror (saved_errno);
            }

          if (reason_to_reopen != NULL)
            {
              /* The log file is either deleted or replaced, probably by a
               * developer who wanted to clear the logs out. Re-create it now
               * instead of staying silent. */
              glnx_autofd int new_fd = -1;
              g_autoptr(GError) local_error = NULL;

              g_info ("Re-opening \"%s\" because: %s",
                      self->filename, reason_to_reopen);

              new_fd = TEMP_FAILURE_RETRY (open (self->filename,
                                                 OPEN_FLAGS,
                                                 0644));
              if (new_fd < 0)
                {
                  _srt_log_warning ("Unable to re-open log file: \"%s\": %s",
                                    self->filename, g_strerror (errno));
                }
              /* Reuse current_stat (which might or might not be populated)
               * for the device and inode of the replacement file */
              else if (fstat (new_fd, &current_stat) < 0)
                {
                  _srt_log_warning ("Unable to stat log file \"%s\": %s",
                                    self->filename, g_strerror (errno));
                }
              else if (!lock_output_file (self->filename, new_fd, &local_error))
                {
                  _srt_log_warning ("Unable to re-lock log file \"%s\": %s",
                                    self->filename, local_error->message);
                }
              else
                {
                  g_info ("Successfully re-opened \"%s\"", self->filename);
                  self->file_fd = glnx_steal_fd (&new_fd);
                  self->file_stat = current_stat;
                }
            }
          else if (self->max_bytes > 0
                   && (current_stat.st_size + len) > self->max_bytes)
            {
              g_autoptr(GError) local_error = NULL;

              if (!_srt_logger_try_rotate (self, &local_error))
                {
                  _srt_log_warning ("Unable to rotate log file: %s",
                                    local_error->message);
                  self->max_bytes = 0;
                }
            }
        }

      if (line_start_time != 0)
        {
          /* We use glibc time formatting rather than GDateTime here,
           * to reduce malloc/free in the main logging loop. */
          char buf[32];
          size_t buf_used = 0;
          struct tm tm;

          /* If we can't format the timestamp for some reason, just don't
           * output it */
          if (localtime_r (&line_start_time, &tm) == &tm)
            {
              buf_used = strftime (buf, sizeof (buf), "[%F %T] ", &tm);
              glnx_loop_write (self->file_fd, buf, buf_used);
            }
        }

      glnx_loop_write (self->file_fd, line, len);
    }
}

/*
 * Finish setup, accept responsibility for logging (which is done by
 * closing @original_stdout), read log lines from standard input and
 * write them to each log sink.
 */
gboolean
_srt_logger_process (SrtLogger *self,
                     int *original_stdout,
                     GError **error)
{
  ssize_t parsed_level_prefix_size = -1;
  int line_level;
  char buf[LINE_MAX + 1] = { 0 };
  /* The portion of the filled buffer that has already been given to
   * logger_process_partial_line (always <= filled) */
  size_t already_processed_partial_line = 0;
  size_t filled = 0;
  ssize_t res;
  time_t line_start_time = 0;

  if (!_srt_logger_setup (self, error))
    return FALSE;

  if (self->filename && self->use_file)
    {
      g_assert (self->file_fd >= 0);

      if (chdir (self->log_dir) != 0)
        return glnx_throw_errno_prefix (error,
                                        "Unable to change to logs directory");

      if (!lock_output_file (self->filename, self->file_fd, error))
        return FALSE;
    }

  if (self->sh_syntax)
    {
      g_autoptr(SrtEnvOverlay) overlay = NULL;
      g_autofree gchar *shell_str = NULL;
      g_autofree gchar *pid_str = NULL;

      overlay = _srt_logger_get_environ (self);
      shell_str = _srt_env_overlay_to_shell (overlay);
      pid_str = g_strdup_printf ("SRT_LOGGER_PID=%" G_PID_FORMAT "\n", getpid ());

      if (glnx_loop_write (*original_stdout, shell_str, strlen (shell_str)) < 0
          || glnx_loop_write (*original_stdout, pid_str, strlen (pid_str)) < 0
          || glnx_loop_write (*original_stdout, READY_MESSAGE, READY_MESSAGE_LEN) < 0)
        return glnx_throw_errno_prefix (error, "Unable to report ready");
    }

  glnx_close_fd (original_stdout);

  do
    {
      gboolean line_overflowed_buffer = FALSE;

      res = TEMP_FAILURE_RETRY (read (STDIN_FILENO,
                                      buf + filled,
                                      sizeof (buf) - filled - 1));

      if (res < 0)
        return glnx_throw_errno_prefix (error, "Error reading standard input");

      if (self->timestamps && filled == 0)
        line_start_time = time (NULL);

      /* We never touch the last byte of buf while reading */
      filled += (size_t) res;
      g_assert (filled < sizeof (buf));

      while (filled > 0)
        {
          char *end_of_line = NULL;
          size_t len;

          g_assert (already_processed_partial_line <= filled);

          /* Skip the parts of the line we know don't have a newline */
          end_of_line = memchr (buf + already_processed_partial_line,
                                '\n',
                                filled - already_processed_partial_line);

          /* If we have read LINE_MAX bytes with no newline, or we reached
           * EOF with no newline at the end, give up and truncate it;
           * otherwise keep reading and wait for a newline to appear. */
          if (end_of_line == NULL)
            {
              if (res == 0 || filled == sizeof (buf) - 1)
                {
                  end_of_line = &buf[filled];
                  *end_of_line = '\n';
                  line_overflowed_buffer = TRUE;
                }
              else
                break;
            }

          /* Length of the first logical line, including the newline */
          len = end_of_line - buf + 1;

          if (already_processed_partial_line > 0)
            {
              /* It shouldn't be possible to have processed part of the line
               * without first having parsed the log level */
              g_assert (parsed_level_prefix_size >= 0);

              /* Since already_processed_partial_line is covering parts that
               * have no newline (because otherwise, it wouldn't be partially
               * processed!), it should always be less than len, which includes
               * the trailing newline */
              g_assert (already_processed_partial_line < len);

              logger_process_partial_line (self,
                                           line_level,
                                           buf + already_processed_partial_line,
                                           len - already_processed_partial_line);
              already_processed_partial_line = 0;
            }
          else
            {
              if (parsed_level_prefix_size < 0)
                {
                  parsed_level_prefix_size =
                    logger_parse_level_prefix (self, buf, len, &line_level);
                  /* This should never fail due to lack of data, because we
                   * already know the line is complete and will end with
                   * newline. */
                  g_assert (parsed_level_prefix_size >= 0);
                }

              if (parsed_level_prefix_size < len)
                logger_process_partial_line (self,
                                             line_level,
                                             buf + parsed_level_prefix_size,
                                             len - parsed_level_prefix_size);
            }

          if (parsed_level_prefix_size < len)
            logger_process_complete_line (self,
                                          line_level,
                                          line_start_time,
                                          buf + parsed_level_prefix_size,
                                          len - parsed_level_prefix_size);

          /* If this is from a single line that overflowed, maintain the same
           * log level for the next read */
          parsed_level_prefix_size = line_overflowed_buffer ? 0 : -1;

          if (filled > len)
            {
              /* sizeof (buf) > filled > len, so this is safe */
              memmove (buf, buf + len, sizeof (buf) - len);
              filled -= len;
            }
          else
            {
              /* All bytes have been drained */
              filled = 0;
            }
        }

      if (filled > already_processed_partial_line)
        {
          /* There is still some leftover content in the buffer that doesn't
           * make up a complete line; we can process it now as long as we know
           * that we've parsed the line's level prefix. Otherwise, we need to
           * to keep buffering until then.
           * */

          if (parsed_level_prefix_size < 0)
            {
              g_assert (already_processed_partial_line == 0);

              parsed_level_prefix_size = logger_parse_level_prefix (self,
                                                                    buf,
                                                                    filled,
                                                                    &line_level);
              if (parsed_level_prefix_size >= 0)
                already_processed_partial_line += parsed_level_prefix_size;
            }

          if (parsed_level_prefix_size >= 0
              /* This condition needs to be rechecked here, because
               * already_processed_partial_line mgiht have been modified by the
               * block above. */
              && filled > already_processed_partial_line)
            {
              logger_process_partial_line (self, line_level,
                                           buf + already_processed_partial_line,
                                           filled - already_processed_partial_line);
              already_processed_partial_line = filled;
            }
        }
    }
  while (res > 0);

  return TRUE;
}

/*
 * _srt_logger_get_environ:
 * @self: The logger
 *
 * Return modifications to be made to the environment to be used for a
 * subprocess so that it will inherit the terminal and Journal
 * settings from this logger.
 *
 * Returns: (transfer full): Modifications to the environment
 */
SrtEnvOverlay *
_srt_logger_get_environ (SrtLogger *self)
{
  g_autoptr(SrtEnvOverlay) overlay = _srt_env_overlay_new ();

  /* The terminal filename is extremely unlikely to include a newline,
   * but if it did, that would break our line-oriented output format...
   * so disallow that. */
  if (self->terminal != NULL
      && G_LIKELY (strchr (self->terminal, '\n') == NULL))
    _srt_env_overlay_set (overlay, "SRT_LOG_TERMINAL", self->terminal);

  /* SRT_LOG_TO_JOURNAL makes steam-runtime-tools utilities log to the
   * Journal *exclusively* (without sending their diagnostic messages
   * to stderr), so only do that if there is no other log destination
   * active. */
  if (self->file_fd < 0
      && self->journal_fd >= 0
      && self->terminal_fd < 0
      && !self->use_stderr)
    {
      _srt_env_overlay_set (overlay, "SRT_LOG_TO_JOURNAL", "1");
    }
  /* If we are outputting to the Journal and at least one other destination,
   * ensure that s-r-t utilities will output to our pipe so that we can send
   * their messages to all destinations. */
  else if (self->journal_fd >= 0)
    {
      _srt_env_overlay_set (overlay, "SRT_LOG_TO_JOURNAL", "0");
      _srt_env_overlay_set (overlay, "SRT_LOGGER_USE_JOURNAL", "1");
    }

  if (self->parse_level_prefix)
    _srt_env_overlay_set (overlay, "SRT_LOG_LEVEL_PREFIX", "1");
  else
    _srt_env_overlay_set (overlay, "SRT_LOG_LEVEL_PREFIX", "0");

  return g_steal_pointer (&overlay);
}
