/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-unix.h>
#include <gio/gio.h>

#include "steam-runtime-tools/launcher-internal.h"
#include "steam-runtime-tools/log-internal.h"
#include "steam-runtime-tools/runtime-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include "libglnx.h"

#define THIS_PROGRAM "srt-run-outside-ldlp"

static unsigned opt_verbose = 0;

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

static const GOptionEntry option_entries[] =
{
  { "verbose", 'v',
    G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, &opt_verbose_cb,
    "Be more verbose. If given twice, log debug messages too.", NULL },
  { NULL }
};

static gboolean
run (int argc,
     char **argv,
     GError **error)
{
  g_autoptr(GOptionContext) option_context = NULL;
  g_auto(GStrv) env = NULL;
  GStrv invocation_argv = argv;
  g_autofree char *invocation_target = NULL;
  const char *exe_name = NULL;
  const char *search_path = NULL;
  glnx_autofd int original_stdout = -1;
  glnx_autofd int original_stderr = -1;

  exe_name = glnx_basename (argv[0]);
  if (g_str_equal (exe_name, THIS_PROGRAM))
    {
      option_context = g_option_context_new ("COMMAND [ARGUMENTS...]");
      g_option_context_add_main_entries (option_context, option_entries, NULL);

      if (!g_option_context_parse (option_context, &argc, &argv, error))
        return FALSE;

      if (argc >= 2 && strcmp (argv[1], "--") == 0)
        {
          argv++;
          argc--;
        }

      if (argc < 2)
        {
          g_autofree gchar *help = g_option_context_get_help (option_context,
                                                              TRUE,
                                                              NULL);
          g_printerr ("Expected at least one argument, a command to run.\n\n");
          g_printerr ("%s\n", help);
          exit (LAUNCH_EX_USAGE);
        }

      exe_name = argv[1];
      invocation_argv = argv + 1;
    }

  if (!_srt_util_set_glib_log_handler (NULL, G_LOG_DOMAIN,
                                       (SRT_LOG_FLAGS_DIVERT_STDOUT
                                        | SRT_LOG_FLAGS_OPTIONALLY_JOURNAL
                                        | (opt_verbose >= 2 ? SRT_LOG_FLAGS_DEBUG : 0)
                                        | (opt_verbose ? SRT_LOG_FLAGS_INFO : 0)),
                                       &original_stdout, &original_stderr,
                                       error))
    return FALSE;

  if (strchr (exe_name, '/') != NULL)
    return glnx_throw (error,
                       "Command to run should not be a path: %s",
                       exe_name);
  else if (g_str_has_prefix (exe_name, "srt-")
           || g_str_has_prefix (exe_name, "steam-runtime-"))
    return glnx_throw (error,
                       "Can't run Steam Runtime command '%s' outside runtime",
                       exe_name);

  if (!_srt_check_recursive_exec_guard (exe_name, error))
    return FALSE;

  env = g_get_environ ();
  env = _srt_environ_escape_steam_runtime (env,
                                           SRT_ESCAPE_RUNTIME_FLAGS_CLEAN_PATH);
  env = g_environ_unsetenv (env, "LD_PRELOAD");
  env = g_environ_setenv (env, _SRT_RECURSIVE_EXEC_GUARD_ENV, THIS_PROGRAM,
                          TRUE);

  search_path = g_environ_getenv (env, "PATH");
  if (search_path == NULL)
    {
      search_path = "/usr/bin:/bin";
      g_warning ("$PATH is not set, defaulting to %s", search_path);
    }

  invocation_target = _srt_find_next_executable (search_path, exe_name, error);
  if (invocation_target == NULL)
    return FALSE;

  if ((original_stdout >= 0
       && !_srt_util_restore_saved_fd (original_stdout, STDOUT_FILENO, error))
      || (original_stderr >= 0
          && !_srt_util_restore_saved_fd (original_stderr, STDERR_FILENO, error)))
    return FALSE;

  execve (invocation_target, invocation_argv, env);

  return glnx_throw_errno_prefix (error, "exec %s", invocation_target);
}

int
main (int argc,
      char **argv)
{
  g_autoptr(GError) error = NULL;
  if (!run (argc, argv, &error))
    {
      if (error == NULL)
        g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED, "Assertion failure");

      _srt_log_failure ("%s", error->message);
      return LAUNCH_EX_FAILED;
    }

  return 0;
}
