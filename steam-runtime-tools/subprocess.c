/*
 * Copyright © 2019-2023 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "steam-runtime-tools/subprocess-internal.h"

#include "steam-runtime-tools/enums.h"
#include "steam-runtime-tools/utils-internal.h"

/* Enabling debug logging for this is rather too verbose, so only
 * enable it when actively debugging this module */
#if 0
#define trace(...) g_debug (__VA_ARGS__)
#else
#define trace(...) do { } while (0)
#endif

static void
gstring_free0 (GString *str)
{
  if (str != NULL)
    g_string_free (str, TRUE);
}

struct _SrtCompletedSubprocess
{
  GObject parent;
  gchar *out;
  gchar *err;
  SrtHelperFlags flags;
  SrtSubprocessOutput out_mode;
  SrtSubprocessOutput err_mode;
  int wait_status;
  unsigned timed_out : 1;
};

struct _SrtCompletedSubprocessClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE (SrtCompletedSubprocess, _srt_completed_subprocess, G_TYPE_OBJECT)

static SrtCompletedSubprocess *
_srt_completed_subprocess_new (void)
{
  return g_object_new (SRT_TYPE_COMPLETED_SUBPROCESS,
                       NULL);
}

static void
_srt_completed_subprocess_init (SrtCompletedSubprocess *self)
{
  self->wait_status = -1;
}

static void
_srt_completed_subprocess_finalize (GObject *object)
{
  SrtCompletedSubprocess *self = SRT_COMPLETED_SUBPROCESS (object);

  g_free (self->out);
  g_free (self->err);

  G_OBJECT_CLASS (_srt_completed_subprocess_parent_class)->finalize (object);
}

static void
_srt_completed_subprocess_class_init (SrtCompletedSubprocessClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->finalize = _srt_completed_subprocess_finalize;
}

static void
_srt_completed_subprocess_dump (SrtCompletedSubprocess *self)
{
  if ((self->out_mode == SRT_SUBPROCESS_OUTPUT_CAPTURE_DEBUG)
      && self->out != NULL
      && self->out[0] != '\0')
    g_debug ("stdout: %s", self->out);

  if ((self->err_mode == SRT_SUBPROCESS_OUTPUT_CAPTURE_DEBUG)
      && self->err != NULL
      && self->err[0] != '\0')
    g_debug ("stderr: %s", self->err);

  g_debug ("Wait status %d", self->wait_status);
}

/*
 * Return %TRUE if @self has completed successfully and had exit status 0.
 * If it exited with an unsuccessful status, attempt to add whatever it
 * wrote to stderr to the error message.
 */
gboolean
_srt_completed_subprocess_check (SrtCompletedSubprocess *self,
                                 GError **error)
{
  gboolean ret;

  _srt_completed_subprocess_dump (self);
  ret = g_spawn_check_wait_status (self->wait_status, error);

  if (error != NULL
      && *error != NULL
      && self->err != NULL
      && self->err[0] != '\0')
    {
      g_autoptr(GError) original = g_steal_pointer (error);

      g_set_error (error, original->domain, original->code,
                   "%s: %s", original->message, self->err);
    }

  return ret;
}

/*
 * _srt_subprocess_report:
 * @self: the subprocess
 * @wait_status_out: (out): a wait()-style status, or -1 if not applicable
 * @exit_status_out: (out): an exit status, or -1 if killed by a signal
 *  or some other non-exit() result
 * @terminating_signal_out: (out): the signal that terminated the process,
 *  or 0 if not terminated by a signal
 * @timed_out_out: (out): %TRUE if the process reached a timeout
 *
 * Return %TRUE if @self has completed successfully, and set
 * various out parameters to reflect further details.
 */
gboolean
_srt_completed_subprocess_report (SrtCompletedSubprocess *self,
                                  int *wait_status_out,
                                  int *exit_status_out,
                                  int *terminating_signal_out,
                                  gboolean *timed_out_out)
{
  if (wait_status_out != NULL)
    *wait_status_out = self->wait_status;

  if (exit_status_out != NULL)
    *exit_status_out = -1;

  if (terminating_signal_out != NULL)
    *terminating_signal_out = 0;

  if (timed_out_out != NULL)
    *timed_out_out = self->timed_out;

  _srt_completed_subprocess_dump (self);

  if (WIFEXITED (self->wait_status))
    {
      int exit_status = WEXITSTATUS (self->wait_status);

      if (exit_status_out != NULL)
        *exit_status_out = exit_status;

      if ((self->flags & SRT_HELPER_FLAGS_SHELL_EXIT_STATUS)
          && exit_status > 128
          && exit_status <= 128 + SIGRTMAX)
        {
          g_debug ("-> subprocess killed by signal %d", (exit_status - 128));

          if (terminating_signal_out != NULL)
            *terminating_signal_out = (exit_status - 128);
        }
      else
        {
          g_debug ("-> exit status %d", exit_status);
        }
    }
  else if (WIFSIGNALED (self->wait_status))
    {
      g_debug ("-> killed by signal %d", WTERMSIG (self->wait_status));

      if (terminating_signal_out != NULL)
        *terminating_signal_out = WTERMSIG (self->wait_status);
    }
  else
    {
      g_critical ("Somehow got a wait_status that was neither exited nor signaled");
      g_return_val_if_reached (FALSE);
    }

  return self->wait_status == 0;
}

/*
 * Return %TRUE if the process timed out, %FALSE if it completed for
 * any other reason.
 */
gboolean
_srt_completed_subprocess_timed_out (SrtCompletedSubprocess *self)
{
  gboolean ret = FALSE;

  _srt_completed_subprocess_report (self, NULL, NULL, NULL, &ret);
  return ret;
}

const char *
_srt_completed_subprocess_get_stdout (SrtCompletedSubprocess *self)
{
  return self->out;
}

const char *
_srt_completed_subprocess_get_stderr (SrtCompletedSubprocess *self)
{
  return self->err;
}

gchar *
_srt_completed_subprocess_steal_stdout (SrtCompletedSubprocess *self)
{
  return g_steal_pointer (&self->out);
}

gchar *
_srt_completed_subprocess_steal_stderr (SrtCompletedSubprocess *self)
{
  return g_steal_pointer (&self->err);
}

typedef struct
{
  /* Thread safety: Not thread safe.
   * All members must only be accessed from the thread where it was created. */
  GString *out;
  GString *err;
  GError *error;
  GMainContext *completing_context;
  GSource *sigterm_source;
  GSource *sigkill_source;
  SrtHelperFlags flags;
  SrtSubprocessOutput out_mode;
  SrtSubprocessOutput err_mode;
  GPid pid;
  unsigned sigterm_seconds;
  unsigned sigkill_seconds;
  int wait_status;
  int stdout_fd;
  int stderr_fd;
  unsigned timed_out : 1;
} SrtSubprocess;

#define SRT_SUBPROCESS_INIT \
{ .wait_status = -1, .stdout_fd = -1, .stderr_fd = -1 }

static void
_srt_subprocess_cancel_timeout (SrtSubprocess *self)
{
  if (self->sigterm_source != NULL)
    g_source_destroy (self->sigterm_source);

  if (self->sigkill_source != NULL)
    g_source_destroy (self->sigkill_source);

  g_clear_pointer (&self->sigterm_source, g_source_unref);
  g_clear_pointer (&self->sigkill_source, g_source_unref);
}

static void
_srt_subprocess_clear (SrtSubprocess *self)
{
  _srt_subprocess_cancel_timeout (self);
  glnx_close_fd (&self->stdout_fd);
  glnx_close_fd (&self->stderr_fd);
  gstring_free0 (self->out);
  gstring_free0 (self->err);
  g_clear_pointer (&self->completing_context, g_main_context_unref);
}

static void
_srt_subprocess_free (void *self)
{
  _srt_subprocess_clear (self);
  g_free (self);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (SrtSubprocess, _srt_subprocess_clear)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtSubprocess, _srt_subprocess_free)

/*
 * Return %TRUE if @self has completed successfully or might still complete
 * successfully in future. Return %FALSE with error if it already failed.
 */
static gboolean
_srt_subprocess_check_no_error (SrtSubprocess *self,
                                GError **error)
{
  if (self->error != NULL)
    {
      g_set_error_literal (error, self->error->domain, self->error->code,
                           self->error->message);
      return FALSE;
    }

  return TRUE;
}

/* Read from stdout or stderr into one of the internal buffers */
static gboolean
_srt_subprocess_read (SrtSubprocess *self,
                      const char *label,
                      GString *string,
                      int *fd_p)
{
  char buf[1024];
  ssize_t len;

  trace ("Data available on process %d %s", self->pid, label);

  len = read (*fd_p, buf, sizeof (buf));

  if (len < 0)
    {
      g_autoptr(GError) error = NULL;
      int saved_errno = errno;

      if (saved_errno == EAGAIN)
        return G_SOURCE_CONTINUE;

      error = g_error_new (G_IO_ERROR,
                           g_io_error_from_errno (saved_errno),
                           "Error reading from subprocess %d %s: %s",
                           self->pid, label, g_strerror (saved_errno));

      g_debug ("%s", error->message);

      if (self->error == NULL)
        self->error = g_steal_pointer (&error);

      glnx_close_fd (fd_p);
      return G_SOURCE_REMOVE;   /* Destroys the source */
    }
  else if (len == 0)
    {
      trace ("EOF reading from subprocess %d %s", self->pid, label);
      glnx_close_fd (fd_p);
      return G_SOURCE_REMOVE;   /* Destroys the source */
    }
  else
    {
      trace ("%zd bytes from subprocess %d %s",
             len, self->pid, label);
      g_string_append_len (string, buf, len);
      return G_SOURCE_CONTINUE;
    }
}

static gboolean
_srt_subprocess_read_stdout_cb (int fd,
                                GIOCondition condition,
                                void *user_data)
{
  SrtSubprocess *self = user_data;

  return _srt_subprocess_read (self, "stdout", self->out, &self->stdout_fd);
}

static gboolean
_srt_subprocess_read_stderr_cb (int fd,
                                GIOCondition condition,
                                void *user_data)
{
  SrtSubprocess *self = user_data;

  return _srt_subprocess_read (self, "stderr", self->err, &self->stderr_fd);
}

/* Iterate @completing_context until everything has been read from stdout
 * and/or stderr */
static void
_srt_subprocess_read_pipes (SrtSubprocess *self)
{
  g_autoptr(GSource) stdout_source = NULL;
  g_autoptr(GSource) stderr_source = NULL;

  if (self->stdout_fd >= 0)
    {
      g_assert (self->out != NULL);
      stdout_source = g_unix_fd_source_new (self->stdout_fd, G_IO_IN);
      g_source_set_callback (stdout_source,
                             G_SOURCE_FUNC (_srt_subprocess_read_stdout_cb),
                             self, NULL);
      g_source_set_name (stdout_source, "read child process stdout");
      g_source_attach (stdout_source, self->completing_context);
    }

  if (self->stderr_fd >= 0)
    {
      g_assert (self->err != NULL);
      stderr_source = g_unix_fd_source_new (self->stderr_fd, G_IO_IN);
      g_source_set_callback (stderr_source,
                             G_SOURCE_FUNC (_srt_subprocess_read_stderr_cb),
                             self, NULL);
      g_source_set_name (stderr_source, "read child process stderr");
      g_source_attach (stderr_source, self->completing_context);
    }

  /* Each fd is closed and cleared when error or EOF is reached */
  while (self->stdout_fd >= 0 || self->stderr_fd >= 0)
    g_main_context_iteration (self->completing_context, TRUE);

  g_assert (stdout_source == NULL || g_source_is_destroyed (stdout_source));
  g_assert (stderr_source == NULL || g_source_is_destroyed (stderr_source));
}

static void
_srt_subprocess_waitpid (SrtSubprocess *self,
                         int waitpid_flags)
{
  pid_t pid;

  g_return_if_fail (self->pid > 0);
  pid = TEMP_FAILURE_RETRY (waitpid (self->pid,
                                     &self->wait_status,
                                     waitpid_flags));

  if (pid < 0)
    {
      int saved_errno = errno;

      g_assert (self->error == NULL);
      self->error = g_error_new (G_IO_ERROR,
                                 g_io_error_from_errno (saved_errno),
                                 "Error waiting for subprocess %d: %s",
                                 self->pid, g_strerror (saved_errno));
      g_debug ("%s", self->error->message);
      self->wait_status = -1;
    }

  if (pid == 0)
    {
      if (waitpid_flags & WNOHANG)
        return;

      /* waitpid() should never return 0 otherwise */
      g_return_if_reached ();
    }

  /* The child process no longer exists, so we must not kill it */
  self->pid = 0;
}

static gboolean
_srt_subprocess_sigkill_cb (void *user_data)
{
  SrtSubprocess *self = user_data;

  trace ("Process %d SIGKILL timeout reached", self->pid);

  self->timed_out = 1;
  g_clear_pointer (&self->sigkill_source, g_source_unref);

  if (self->pid > 0)
    {
      g_debug ("Process %d timed out, sending SIGKILL", self->pid);
      kill (self->pid, SIGKILL);
      kill (self->pid, SIGCONT);
    }

  return G_SOURCE_REMOVE;
}

static void
_srt_subprocess_schedule_sigkill (SrtSubprocess *self)
{
  g_return_if_fail (self->pid > 0);
  trace ("Scheduling SIGKILL after %u seconds", self->sigkill_seconds);
  self->sigkill_source = g_timeout_source_new_seconds (self->sigkill_seconds);
  g_source_set_callback (self->sigkill_source, _srt_subprocess_sigkill_cb, self, NULL);
  g_source_set_name (self->sigkill_source, "send SIGKILL to timed-out child");
  g_source_attach (self->sigkill_source, self->completing_context);
}

static gboolean
_srt_subprocess_sigterm_cb (void *user_data)
{
  SrtSubprocess *self = user_data;

  trace ("Process %d SIGTERM timeout reached", self->pid);

  self->timed_out = 1;
  g_clear_pointer (&self->sigterm_source, g_source_unref);

  if (self->pid > 0)
    {
      g_debug ("Process %d timed out, sending SIGTERM", self->pid);
      kill (self->pid, SIGTERM);

      if (self->sigkill_seconds > 0)
        _srt_subprocess_schedule_sigkill (self);
    }

  return G_SOURCE_REMOVE;
}

static void
_srt_subprocess_schedule_sigterm (SrtSubprocess *self)
{
  g_return_if_fail (self->pid > 0);
  trace ("Scheduling SIGTERM after %u seconds", self->sigterm_seconds);
  self->sigterm_source = g_timeout_source_new_seconds (self->sigterm_seconds);
  g_source_set_callback (self->sigterm_source, _srt_subprocess_sigterm_cb, self, NULL);
  g_source_set_name (self->sigterm_source, "send SIGTERM to timed-out child");
  g_source_attach (self->sigterm_source, self->completing_context);
}

static gboolean
_srt_subprocess_poll_cb (void *user_data)
{
  SrtSubprocess *self = user_data;

  _srt_subprocess_waitpid (self, WNOHANG);

  if (self->pid == 0)
    {
      _srt_subprocess_cancel_timeout (self);
      return G_SOURCE_REMOVE;
    }
  else
    {
      return G_SOURCE_CONTINUE;
    }
}

/* Similar to g_subprocess_communicate() */
static void
_srt_subprocess_complete_sync (SrtSubprocess *self)
{
  g_return_if_fail (self->completing_context == NULL);
  self->completing_context = g_main_context_new ();

  g_main_context_push_thread_default (self->completing_context);
    {
      if (self->sigterm_seconds > 0)
        _srt_subprocess_schedule_sigterm (self);
      else if (self->sigkill_seconds > 0)
        _srt_subprocess_schedule_sigkill (self);

      _srt_subprocess_read_pipes (self);

      if (self->flags & SRT_HELPER_FLAGS_TIME_OUT)
        {
          g_autoptr(GSource) tick_source = NULL;

          /* Opportunistically check whether the process already exited:
           * if it has, there's no need to go to the expense of creating a
           * thread to wait for it. */
          _srt_subprocess_waitpid (self, WNOHANG);

          if (self->pid == 0)
            goto out;

          tick_source = g_timeout_source_new (100);
          g_source_set_callback (tick_source, _srt_subprocess_poll_cb,
                                 self, NULL);
          g_source_set_name (tick_source, "poll child process");
          g_source_attach (tick_source, self->completing_context);

          while (self->pid != 0)
            g_main_context_iteration (self->completing_context, TRUE);

          g_source_destroy (tick_source);
        }
      else
        {
          /* There's no timeout, so we can just wait forever */
          _srt_subprocess_waitpid (self, 0);
        }
    }
out:
  g_main_context_pop_thread_default (self->completing_context);
}

static SrtCompletedSubprocess *
_srt_completed_subprocess_new_from_subprocess (SrtSubprocess *self)
{
  g_autoptr(SrtCompletedSubprocess) completed = _srt_completed_subprocess_new ();

  completed->flags = self->flags;
  completed->out_mode = self->out_mode;
  completed->err_mode = self->err_mode;
  completed->wait_status = self->wait_status;
  completed->timed_out = self->timed_out;

  if (self->out != NULL)
    completed->out = g_string_free (g_steal_pointer (&self->out), FALSE);

  if (self->err != NULL)
    completed->err = g_string_free (g_steal_pointer (&self->err), FALSE);

  return g_steal_pointer (&completed);
}

struct _SrtSubprocessRunner
{
  GObject parent;
  /* Environment */
  GStrv envp;
  /* Path to find helper executables, or %NULL to use $SRT_HELPERS_PATH
   * or the installed helpers */
  gchar *helpers_path;
  SrtTestFlags test_flags;
  unsigned can_use_waitid : 1;
};

struct _SrtSubprocessRunnerClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE (SrtSubprocessRunner, _srt_subprocess_runner, G_TYPE_OBJECT)

enum
{
  PROP_0,
  PROP_ENVIRON,
  PROP_HELPERS_PATH,
  PROP_TEST_FLAGS,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL };

static void
_srt_subprocess_runner_init (SrtSubprocessRunner *self)
{
}

static void
_srt_subprocess_runner_constructed (GObject *object)
{
  SrtSubprocessRunner *self = SRT_SUBPROCESS_RUNNER (object);

  if (self->envp == NULL)
    self->envp = _srt_filter_gameoverlayrenderer_from_envp (_srt_peek_environ_nonnull ());

  G_OBJECT_CLASS (_srt_subprocess_runner_parent_class)->constructed (object);
}

static void
_srt_subprocess_runner_get_property (GObject *object,
                                     guint prop_id,
                                     GValue *value,
                                     GParamSpec *pspec)
{
  SrtSubprocessRunner *self = SRT_SUBPROCESS_RUNNER (object);

  switch (prop_id)
    {
      case PROP_ENVIRON:
        g_value_set_boxed (value, self->envp);
        break;

      case PROP_HELPERS_PATH:
        g_value_set_string (value, self->helpers_path);
        break;

      case PROP_TEST_FLAGS:
        g_value_set_flags (value, self->test_flags);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
_srt_subprocess_runner_set_property (GObject *object,
                                     guint prop_id,
                                     const GValue *value,
                                     GParamSpec *pspec)
{
  SrtSubprocessRunner *self = SRT_SUBPROCESS_RUNNER (object);
  const char * const *envp;

  switch (prop_id)
    {
      case PROP_ENVIRON:
        /* Construct-only */
        g_return_if_fail (self->envp == NULL);
        envp = g_value_get_boxed (value);

        if (envp != NULL)
          self->envp = _srt_filter_gameoverlayrenderer_from_envp (envp);

        break;

      case PROP_HELPERS_PATH:
        /* Construct-only */
        g_return_if_fail (self->helpers_path == NULL);
        self->helpers_path = g_value_dup_string (value);
        break;

      case PROP_TEST_FLAGS:
        /* Construct-only */
        g_return_if_fail (self->test_flags == SRT_TEST_FLAGS_NONE);
        self->test_flags = g_value_get_flags (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
_srt_subprocess_runner_finalize (GObject *object)
{
  SrtSubprocessRunner *self = SRT_SUBPROCESS_RUNNER (object);

  g_strfreev (self->envp);
  g_free (self->helpers_path);

  G_OBJECT_CLASS (_srt_subprocess_runner_parent_class)->finalize (object);
}

static void
_srt_subprocess_runner_class_init (SrtSubprocessRunnerClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->constructed = _srt_subprocess_runner_constructed;
  object_class->get_property = _srt_subprocess_runner_get_property;
  object_class->set_property = _srt_subprocess_runner_set_property;
  object_class->finalize = _srt_subprocess_runner_finalize;

  properties[PROP_ENVIRON] =
    g_param_spec_boxed ("environ", NULL, NULL, G_TYPE_STRV,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);
  properties[PROP_HELPERS_PATH] =
    g_param_spec_string ("helpers-path", NULL, NULL, NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  properties[PROP_TEST_FLAGS] =
    g_param_spec_flags ("test-flags", NULL, NULL,
                        SRT_TYPE_TEST_FLAGS, SRT_TEST_FLAGS_NONE,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

/*
 * Returns: The environment. Never %NULL unless a programming error occurs.
 */
const char * const *
_srt_subprocess_runner_get_environ (SrtSubprocessRunner *self)
{
  g_return_val_if_fail (SRT_IS_SUBPROCESS_RUNNER (self), NULL);
  return (const char * const *) self->envp;
}

/*
 * Returns: The value of environment variable @var, or %NULL if unset.
 */
const char *
_srt_subprocess_runner_getenv (SrtSubprocessRunner *self,
                               const char *var)
{
  g_return_val_if_fail (SRT_IS_SUBPROCESS_RUNNER (self), NULL);
  return g_environ_getenv (self->envp, var);
}

/*
 * Returns: The path to `x86_64-linux-gnu-check-gl` and so on,
 *  or %NULL to use a default.
 */
const char *
_srt_subprocess_runner_get_helpers_path (SrtSubprocessRunner *self)
{
  g_return_val_if_fail (SRT_IS_SUBPROCESS_RUNNER (self), NULL);
  return self->helpers_path;
}

/*
 * Returns: Test flags
 */
SrtTestFlags
_srt_subprocess_runner_get_test_flags (SrtSubprocessRunner *self)
{
  g_return_val_if_fail (SRT_IS_SUBPROCESS_RUNNER (self), SRT_TEST_FLAGS_NONE);
  return self->test_flags;
}

/*
 * _srt_subprocess_runner_get_helper:
 * @runner: The execution environment
 * @multiarch: (nullable): A multiarch tuple like %SRT_ABI_I386 to prefix
 *  to the executable name, or %NULL
 * @base: (not nullable): Base name of the executable
 * @flags: Flags affecting how we set up the helper
 * @error: Used to raise an error if %NULL is returned
 *
 * Find a helper executable. We return an array of arguments so that the
 * helper can be wrapped by an "adverb" like `env`, `timeout` or a
 * specific `ld.so` implementation if required.
 *
 * Returns: (nullable) (element-type filename) (transfer container): The
 *  initial `argv` for the helper, with g_free() set as the free-function, and
 *  no %NULL terminator. Free with g_ptr_array_unref() or g_ptr_array_free().
 */
GPtrArray *
_srt_subprocess_runner_get_helper (SrtSubprocessRunner *self,
                                   const char *multiarch,
                                   const char *base,
                                   SrtHelperFlags flags,
                                   GError **error)
{
  const char *helpers_path;
  GPtrArray *argv = NULL;
  gchar *path;
  gchar *prefixed;

  g_return_val_if_fail (_srt_check_not_setuid (), NULL);
  g_return_val_if_fail (base != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  argv = g_ptr_array_new_with_free_func (g_free);
  helpers_path = self->helpers_path;

  if (helpers_path == NULL)
    helpers_path = g_getenv ("SRT_HELPERS_PATH");

  if (helpers_path == NULL
      && _srt_find_myself (&helpers_path, error) == NULL)
    {
      g_ptr_array_unref (argv);
      return NULL;
    }

  /* Prefer a helper from ${SRT_HELPERS_PATH} or
   * ${libexecdir}/steam-runtime-tools-${_SRT_API_MAJOR}
   * if it exists */
  path = g_strdup_printf ("%s/%s%s%s",
                          helpers_path,
                          multiarch == NULL ? "" : multiarch,
                          multiarch == NULL ? "" : "-",
                          base);

  g_debug ("Looking for %s", path);

  if (g_file_test (path, G_FILE_TEST_IS_EXECUTABLE))
    {
      g_ptr_array_add (argv, g_steal_pointer (&path));
      return argv;
    }

  if ((flags & SRT_HELPER_FLAGS_SEARCH_PATH) == 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "%s not found", path);
      g_free (path);
      g_ptr_array_unref (argv);
      return NULL;
    }

  /* For helpers that are not part of steam-runtime-tools
   * (historically this included *-wflinfo), we fall back to searching $PATH */
  g_free (path);

  if (multiarch == NULL)
    prefixed = g_strdup (base);
  else
    prefixed = g_strdup_printf ("%s-%s", multiarch, base);

  g_ptr_array_add (argv, g_steal_pointer (&prefixed));
  return argv;
}

static gboolean
_srt_subprocess_runner_spawn (SrtSubprocessRunner *self,
                              SrtHelperFlags flags,
                              const char * const *argv,
                              SrtSubprocessOutput stdout_mode,
                              SrtSubprocessOutput stderr_mode,
                              SrtSubprocess *subprocess,
                              GError **error)
{
  g_auto(GStrv) my_environ = NULL;
  GSpawnFlags spawn_flags = G_SPAWN_DO_NOT_REAP_CHILD;
  int *stdout_fdp = NULL;
  int *stderr_fdp = NULL;
  unsigned sigterm_seconds = 0;
  unsigned sigkill_seconds = 0;

  if (flags & SRT_HELPER_FLAGS_LIBGL_VERBOSE)
    {
      if (my_environ == NULL)
        my_environ = g_strdupv (self->envp);

      my_environ = g_environ_setenv (my_environ, "LIBGL_DEBUG", "verbose", TRUE);
    }

  if (flags & SRT_HELPER_FLAGS_SEARCH_PATH)
    spawn_flags |= G_SPAWN_SEARCH_PATH;

  if ((flags & SRT_HELPER_FLAGS_TIME_OUT) == 0)
    {
      trace ("Unlimited timeout");
    }
  else
    {
      if (self->test_flags & SRT_TEST_FLAGS_TIME_OUT_SOONER)
        {
          /* Speed up the failing case in automated testing */
          sigterm_seconds = sigkill_seconds = 1;
        }
      else
        {
          /* Send SIGTERM after 10 seconds. If still running 3 seconds later,
           * send SIGKILL */
          sigterm_seconds = 10;
          sigkill_seconds = 3;
        }
    }

  subprocess->flags = flags;
  subprocess->out_mode = stdout_mode;
  subprocess->err_mode = stderr_mode;
  subprocess->sigterm_seconds = sigterm_seconds;
  subprocess->sigkill_seconds = sigkill_seconds;

  switch (stdout_mode)
    {
      case SRT_SUBPROCESS_OUTPUT_CAPTURE:
      case SRT_SUBPROCESS_OUTPUT_CAPTURE_DEBUG:
        subprocess->out = g_string_new ("");
        stdout_fdp = &subprocess->stdout_fd;
        break;

      case SRT_SUBPROCESS_OUTPUT_INHERIT:
        break;

      case SRT_SUBPROCESS_OUTPUT_SILENCE:
        spawn_flags |= G_SPAWN_STDOUT_TO_DEV_NULL;
        break;

      default:
        g_return_val_if_reached (FALSE);
    }

  switch (stderr_mode)
    {
      case SRT_SUBPROCESS_OUTPUT_CAPTURE:
      case SRT_SUBPROCESS_OUTPUT_CAPTURE_DEBUG:
        subprocess->err = g_string_new ("");
        stderr_fdp = &subprocess->stderr_fd;
        break;

      case SRT_SUBPROCESS_OUTPUT_INHERIT:
        break;

      case SRT_SUBPROCESS_OUTPUT_SILENCE:
        spawn_flags |= G_SPAWN_STDERR_TO_DEV_NULL;
        break;

      default:
        g_return_val_if_reached (FALSE);
    }

  return g_spawn_async_with_pipes (NULL,        /* working directory */
                                   (gchar **) argv,
                                   my_environ != NULL ? my_environ : self->envp,
                                   spawn_flags,
                                   _srt_child_setup_unblock_signals,
                                   NULL,        /* user data */
                                   &subprocess->pid,
                                   NULL,
                                   stdout_fdp,
                                   stderr_fdp,
                                   error);
}

SrtCompletedSubprocess *
_srt_subprocess_runner_run_sync (SrtSubprocessRunner *self,
                                 SrtHelperFlags flags,
                                 const char * const *argv,
                                 SrtSubprocessOutput stdout_mode,
                                 SrtSubprocessOutput stderr_mode,
                                 GError **error)
{
  g_auto(SrtSubprocess) subprocess = SRT_SUBPROCESS_INIT;

  if (!_srt_subprocess_runner_spawn (self,
                                     flags, argv, stdout_mode, stderr_mode,
                                     &subprocess, error))
    return NULL;

  _srt_subprocess_complete_sync (&subprocess);

  if (!_srt_subprocess_check_no_error (&subprocess, error))
    return NULL;

  return _srt_completed_subprocess_new_from_subprocess (&subprocess);
}
