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
  int child_ready_to_parent;
  int pipe_from_parent;
  int original_stderr;
  int file_fd;
  int journal_fd;
  int terminal_fd;
  goffset max_bytes;
  unsigned background : 1;
  unsigned sh_syntax : 1;
  unsigned use_file : 1;
  unsigned use_journal : 1;
  unsigned use_stderr : 1;
  unsigned use_terminal : 1;
};

struct _SrtLoggerClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE (SrtLogger, _srt_logger, G_TYPE_OBJECT)

static void
_srt_logger_init (SrtLogger *self)
{
  self->child_ready_to_parent = -1;
  self->pipe_from_parent = -1;
  self->original_stderr = -1;
  self->file_fd = -1;
  self->journal_fd = -1;
  self->terminal_fd = -1;
  self->max_bytes = -1;
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
 *
 * Create a new logger. All parameters are "stolen".
 */
SrtLogger *
_srt_logger_new_take (char *argv0,
                      gboolean background,
                      char *filename,
                      int file_fd,
                      char *identifier,
                      gboolean journal,
                      int journal_fd,
                      char *log_dir,
                      goffset max_bytes,
                      int original_stderr,
                      gboolean sh_syntax,
                      gboolean terminal,
                      int terminal_fd)
{
  SrtLogger *self = g_object_new (SRT_TYPE_LOGGER, NULL);

  self->argv0 = argv0;
  self->background = !!background;
  self->filename = filename;
  self->file_fd = file_fd;
  self->identifier = identifier;
  self->journal_fd = journal_fd;
  self->log_dir = log_dir;
  self->max_bytes = max_bytes;
  self->use_file = TRUE;
  self->use_journal = !!journal;
  self->original_stderr = original_stderr;
  self->sh_syntax = !!sh_syntax;
  self->use_terminal = !!terminal;
  self->terminal_fd = terminal_fd;
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
                                                 FALSE,
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

      message = g_string_new (g_get_prgname ());
      date_time = g_date_time_new_now_local ();
      timestamp = g_date_time_format (date_time, "%F %T%z");
      g_string_append_printf (message, "[%d]: Log opened %s\n", getpid (), timestamp);
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

  if (TEMP_FAILURE_RETRY (rename (self->new_filename, self->filename)) != 0)
    {
      glnx_throw_errno_prefix (error, "Unable to rename %s to %s",
                               self->new_filename, self->filename);
      goto out;
    }

  glnx_close_fd (&self->file_fd);
  self->file_fd = g_steal_fd (&new_fd);
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
                             const char *line,
                             size_t len)
{
  if (self->use_stderr)
    glnx_loop_write (STDERR_FILENO, line, len);

  if (self->terminal_fd >= 0)
    glnx_loop_write (self->terminal_fd, line, len);
}

/*
 * @self: The logger
 * @line: (array length=len): Pointer to the beginning of a log message
 * @len: Number of bytes to process, including the trailing newline
 *
 * Send @len bytes of @line to destinations that expect to receive complete
 * log lines: the systemd Journal (if used), and a rotating
 * log file (if used).
 */
static void
logger_process_complete_line (SrtLogger *self,
                              const char *line,
                              size_t len)
{
  if (self->journal_fd >= 0)
    glnx_loop_write (self->journal_fd, line, len);

  if (self->file_fd >= 0)
    {
      struct stat stat_buf;

      if (self->max_bytes > 0
          && self->filename != NULL
          && fstat (self->file_fd, &stat_buf) == 0
          && (stat_buf.st_size + len) > self->max_bytes)
        {
          g_autoptr(GError) local_error = NULL;

          if (!_srt_logger_try_rotate (self, &local_error))
            {
              _srt_log_warning ("Unable to rotate log file: %s",
                                local_error->message);
              self->max_bytes = 0;
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
  char buf[LINE_MAX + 1] = { 0 };
  size_t filled = 0;
  ssize_t res;

  if (!_srt_logger_setup (self, error))
    return FALSE;

  if (self->filename && self->use_file)
    {
      struct flock shared_lock = SHARED_LOCK;

      g_assert (self->file_fd >= 0);

      if (chdir (self->log_dir) != 0)
        return glnx_throw_errno_prefix (error,
                                        "Unable to change to logs directory");

      /* Fall back from OFD locking to legacy POSIX locking if necessary:
       * in this case we will not rotate logs */
      if (TEMP_FAILURE_RETRY (fcntl (self->file_fd,
                                     F_OFD_SETLKW,
                                     &shared_lock)) != 0
          && (errno != EINVAL
              || TEMP_FAILURE_RETRY (fcntl (self->file_fd,
                                            F_SETLKW,
                                            &shared_lock)) != 0))
        return glnx_throw_errno_prefix (error, "Unable to take shared lock on %s",
                                        self->filename);
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
      res = TEMP_FAILURE_RETRY (read (STDIN_FILENO,
                                      buf + filled,
                                      sizeof (buf) - filled - 1));

      if (res < 0)
        return glnx_throw_errno_prefix (error, "Error reading standard input");

      logger_process_partial_line (self, buf + filled, (size_t) res);

      /* We never touch the last byte of buf while reading */
      filled += (size_t) res;
      g_assert (filled < sizeof (buf));

      while (filled > 0)
        {
          char *end_of_line = memchr (buf, '\n', filled);
          size_t len;

          /* If we have read LINE_MAX bytes with no newline, or we reached
           * EOF with no newline at the end, give up and truncate it;
           * otherwise keep reading and wait for a newline to appear. */
          if (end_of_line == NULL)
            {
              if (res == 0 || filled == sizeof (buf) - 1)
                {
                  end_of_line = &buf[filled];
                  *end_of_line = '\n';
                }
              else
                break;
            }

          /* Length of the first logical line, including the newline */
          len = end_of_line - buf + 1;

          logger_process_complete_line (self, buf, len);

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

  return g_steal_pointer (&overlay);
}
