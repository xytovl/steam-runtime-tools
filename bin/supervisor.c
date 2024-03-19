/*
 * Run and supervise subprocesses.
 *
 * Copyright © 2019-2024 Collabora Ltd.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <locale.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include "libglnx.h"

#include "steam-runtime-tools/env-overlay-internal.h"
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/file-lock-internal.h"
#include "steam-runtime-tools/launcher-internal.h"
#include "steam-runtime-tools/log-internal.h"
#include "steam-runtime-tools/process-manager-internal.h"
#include "steam-runtime-tools/utils-internal.h"

static SrtProcessManagerOptions *global_options = NULL;
static gboolean opt_clear_env = FALSE;
static gboolean opt_close_fds = FALSE;
static gboolean opt_exit_with_parent = FALSE;
static gboolean opt_lock_create = FALSE;
static gboolean opt_lock_exclusive = FALSE;
static gboolean opt_lock_verbose = FALSE;
static gboolean opt_lock_wait = FALSE;
static gboolean opt_subreaper = FALSE;
static double opt_terminate_idle_timeout = 0.0;
static double opt_terminate_timeout = -1.0;
static unsigned opt_verbose = 0;
static gboolean opt_version = FALSE;

static gboolean
opt_assign_fd_cb (const char *name,
                  const char *value,
                  gpointer data,
                  GError **error)
{
  return _srt_process_manager_options_assign_fd_cli (global_options,
                                                     name, value, error);
}

static gboolean
opt_lock_fd_cb (const char *name,
                const char *value,
                gpointer data,
                GError **error)
{
  return _srt_process_manager_options_lock_fd_cli (global_options,
                                                   name, value, error);
}

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
opt_lock_file_cb (const char *name,
                  const char *value,
                  gpointer data,
                  GError **error)
{
  SrtFileLock *lock;
  SrtFileLockFlags flags = SRT_FILE_LOCK_FLAGS_NONE;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  if (opt_lock_create)
    flags |= SRT_FILE_LOCK_FLAGS_CREATE;

  if (opt_lock_exclusive)
    flags |= SRT_FILE_LOCK_FLAGS_EXCLUSIVE;

  if (opt_lock_verbose)
    flags |= SRT_FILE_LOCK_FLAGS_VERBOSE;

  if (opt_lock_wait)
    flags |= SRT_FILE_LOCK_FLAGS_WAIT;

  lock = srt_file_lock_new (AT_FDCWD, value, flags, error);

  if (lock == NULL)
    return FALSE;

  _srt_process_manager_options_take_lock (global_options,
                                          g_steal_pointer (&lock));
  return TRUE;
}

static gboolean
opt_pass_fd_cb (const char *name,
                const char *value,
                gpointer data,
                GError **error)
{
  return _srt_process_manager_options_pass_fd_cli (global_options,
                                                   name, value, error);
}

static GOptionEntry options[] =
{
  { "assign-fd", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, opt_assign_fd_cb,
    "Make fd TARGET a copy of SOURCE, like TARGET>&SOURCE in shell.",
    "TARGET=SOURCE" },

  { "clear-env", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_clear_env,
    "Run with clean environment.", NULL },

  { "close-fds", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_close_fds,
    "Close all file descriptors other than standard input/output/error "
    "and those specified with --assign-fd or --pass-fd.",
    NULL },
  { "no-close-fds", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_close_fds,
    "Don't close inherited file descriptors [default].",
    NULL },

  { "exit-with-parent", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_exit_with_parent,
    "Terminate child process and self with SIGTERM when parent process "
    "exits.",
    NULL },
  { "no-exit-with-parent", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_exit_with_parent,
    "Don't do anything special when parent process exits [default].",
    NULL },

  { "lock-create", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_lock_create,
    "Create each subsequent lock file if it doesn't exist.",
    NULL },
  { "no-lock-create", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_lock_create,
    "Don't create subsequent nonexistent lock files [default].",
    NULL },

  { "lock-exclusive", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_lock_exclusive,
    "Lock each subsequent lock file for exclusive access.",
    NULL },
  { "lock-shared", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_lock_exclusive,
    "Lock each subsequent lock file for shared access [default].",
    NULL },
  { "no-lock-exclusive", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_lock_exclusive,
    "Alias for --lock-shared.",
    NULL },

  { "lock-fd", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, opt_lock_fd_cb,
    "Take a file descriptor, already locked if desired, and keep it "
    "open. May be repeated.",
    "FD" },

  { "lock-file", '\0',
    G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, opt_lock_file_cb,
    "Open the given file and lock it, affected by options appearing "
    "earlier on the command-line. May be repeated.",
    "PATH" },

  { "lock-verbose", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_lock_verbose,
    "Log messages if a lock cannot be acquired immediately.",
    NULL },
  { "no-lock-verbose", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_lock_verbose,
    "Don't log messages if a lock-file is busy [default].",
    NULL },

  { "lock-wait", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_lock_wait,
    "Wait for each subsequent lock file.",
    NULL },
  { "no-lock-wait", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_lock_wait,
    "Exit unsuccessfully if a lock-file is busy [default].",
    NULL },

  { "pass-fd", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, opt_pass_fd_cb,
    "Let the launched process inherit the given fd, despite --close-fds.",
    "FD" },

  { "subreaper", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_subreaper,
    "Do not exit until all descendant processes have exited.",
    NULL },
  { "no-subreaper", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_subreaper,
    "Only wait for a direct child process [default].",
    NULL },

  { "terminate-idle-timeout", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_DOUBLE, &opt_terminate_idle_timeout,
    "If --terminate-timeout is used, wait this many seconds before "
    "sending SIGTERM. [default: 0.0]",
    "SECONDS" },
  { "terminate-timeout", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_DOUBLE, &opt_terminate_timeout,
    "Send SIGTERM and SIGCONT to descendant processes that didn't "
    "exit within --terminate-idle-timeout. If they don't all exit within "
    "this many seconds, send SIGKILL and SIGCONT to survivors. If 0.0, "
    "skip SIGTERM and use SIGKILL immediately. Implies --subreaper. "
    "[Default: -1.0, meaning don't signal].",
    "SECONDS" },

  { "verbose", 'v',
    G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_verbose_cb,
    "Be more verbose. If given twice, log debug messages too.", NULL },
  { "version", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_version,
    "Print version number and exit.", NULL },
  { NULL }
};

int
main (int argc,
      char *argv[])
{
  g_auto(SrtProcessManagerOptions) process_manager_options = SRT_PROCESS_MANAGER_OPTIONS_INIT;
  g_auto(GStrv) envp = NULL;
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(SrtEnvOverlay) env_overlay = NULL;
  g_autoptr(SrtProcessManager) process_manager = NULL;
  GError **error = &local_error;
  glnx_autofd int original_stdout = -1;
  glnx_autofd int original_stderr = -1;
  int ret = LAUNCH_EX_FAILED;

  setlocale (LC_ALL, "");

  global_options = &process_manager_options;
  envp = g_get_environ ();

  /* Set up the initial base logging */
  if (!_srt_util_set_glib_log_handler ("steam-runtime-supervisor",
                                       G_LOG_DOMAIN,
                                       SRT_LOG_FLAGS_DIVERT_STDOUT,
                                       &original_stdout, &original_stderr,
                                       error))
    goto out;

  context = g_option_context_new (
      "COMMAND [ARG...]\n"
      "Run COMMAND [ARG...] with a lock held, a subreaper, or similar.\n");

  g_option_context_add_main_entries (context, options, NULL);

  env_overlay = _srt_env_overlay_new ();
  g_option_context_add_group (context,
                              _srt_env_overlay_create_option_group (env_overlay));

  if (!g_option_context_parse (context, &argc, &argv, error))
    goto out;

  g_clear_pointer (&context, g_option_context_free);

  if (opt_version)
    {
      g_print ("%s:\n"
               " Package: steam-runtime-tools\n"
               " Version: %s\n",
               argv[0], VERSION);
      ret = 0;
      goto out;
    }

  if (!_srt_util_set_glib_log_handler (NULL, G_LOG_DOMAIN,
                                       (SRT_LOG_FLAGS_DIVERT_STDOUT
                                        | SRT_LOG_FLAGS_OPTIONALLY_JOURNAL
                                        | (opt_verbose >= 2 ? SRT_LOG_FLAGS_DEBUG : 0)
                                        | (opt_verbose ? SRT_LOG_FLAGS_INFO : 0)),
                                       NULL, NULL, error))
    goto out;

  /* Must be called before we start any threads, but after we set up
   * logging */
  if (!_srt_process_manager_init_single_threaded (error))
    goto out;

  _srt_setenv_disable_gio_modules ();

  if (argc >= 2 && strcmp (argv[1], "--") == 0)
    {
      argv++;
      argc--;
    }

  if (argc < 2)
    {
      g_printerr ("%s: Usage: %s [OPTIONS] COMMAND [ARG...]\n",
                  g_get_prgname (),
                  g_get_prgname ());
      goto out;
    }

  process_manager_options.close_fds = !!opt_close_fds;
  process_manager_options.dump_parameters = TRUE;
  process_manager_options.exit_with_parent = !!opt_exit_with_parent;
  process_manager_options.forward_signals = TRUE;
  process_manager_options.subreaper = (opt_subreaper || opt_terminate_timeout >= 0);

  if (opt_terminate_idle_timeout > 0.0)
    process_manager_options.terminate_wait_usec = opt_terminate_idle_timeout * G_TIME_SPAN_SECOND;

  if (opt_terminate_timeout >= 0.0)
    process_manager_options.terminate_grace_usec = opt_terminate_timeout * G_TIME_SPAN_SECOND;

  /* In the absence of --assign-fd arguments, the default is like shell
   * redirection 1>&original_stdout 2>&original_stderr */
  _srt_process_manager_options_take_original_stdout_stderr (&process_manager_options,
                                                            glnx_steal_fd (&original_stdout),
                                                            glnx_steal_fd (&original_stderr));

  global_options = NULL;
  process_manager = _srt_process_manager_new (&process_manager_options, error);

  if (process_manager == NULL)
    goto out;

  if (opt_clear_env)
    g_clear_pointer (&envp, g_strfreev);

  envp = _srt_env_overlay_apply (env_overlay, envp);
  g_clear_pointer (&env_overlay, _srt_env_overlay_unref);

  /* We take the same action whether this succeeds or fails */
  _srt_process_manager_run (process_manager,
                            (const char * const *) &argv[1],
                            (const char * const *) envp,
                            error);
  ret = _srt_process_manager_get_exit_status (process_manager);

out:
  global_options = NULL;

  if (local_error != NULL)
    _srt_log_failure ("%s", local_error->message);

  g_debug ("Exiting with status %d", ret);
  return ret;
}
