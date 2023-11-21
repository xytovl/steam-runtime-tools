/* pressure-vessel-verify — verify SteamLinuxRuntime_* against a manifest
 *
 * Copyright © 2023 Collabora Ltd.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <locale.h>
#include <sysexits.h>
#include <unistd.h>

#include "libglnx.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/log-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include "mtree.h"

static gboolean opt_minimized_runtime = FALSE;
static gchar *opt_mtree = NULL;
static gboolean opt_quiet = FALSE;
static gboolean opt_verbose = FALSE;
static gboolean opt_version = FALSE;

static GOptionEntry options[] =
{
  { "minimized-runtime", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_minimized_runtime,
    "Verify a minimized runtime.", NULL },
  { "mtree", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_mtree,
    "Verify a manifest other than the default filename.", NULL },
  { "quiet", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_quiet,
    "Be less verbose.", NULL },
  { "verbose", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_verbose,
    "Be more verbose.", NULL },
  { "version", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_version,
    "Print version number and exit.", NULL },
  { NULL }
};

static void
no_log_handler (const gchar *log_domain,
                GLogLevelFlags log_level,
                const gchar *message,
                gpointer user_data)
{
}

static gboolean
run (int argc,
     char **argv,
     GError **error)
{
  g_autoptr(GOptionContext) context = NULL;
  g_autofree gchar *mtree = NULL;
  g_autofree gchar *top = NULL;
  glnx_autofd int top_fd = -1;
  PvMtreeApplyFlags flags = PV_MTREE_APPLY_FLAGS_GZIP;

  setlocale (LC_ALL, "");

  if (!_srt_util_set_glib_log_handler ("pv-verify",
                                       G_LOG_DOMAIN,
                                       (SRT_LOG_FLAGS_OPTIONALLY_JOURNAL
                                        | SRT_LOG_FLAGS_DIVERT_STDOUT),
                                       NULL, NULL, error))
    return FALSE;

  context = g_option_context_new ("DIRECTORY\n"
                                  "DIRECTORY is SteamLinuxRuntime_sniper or similar.\n");
  g_option_context_add_main_entries (context, options, NULL);

  if (!g_option_context_parse (context, &argc, &argv, error))
    return FALSE;

  if (opt_version)
    {
      g_print ("%s:\n"
               " Package: steam-runtime-tools\n"
               " Version: %s\n",
               g_get_prgname (), VERSION);
      return TRUE;
    }

  if (!_srt_util_set_glib_log_handler (NULL, G_LOG_DOMAIN,
                                       (SRT_LOG_FLAGS_OPTIONALLY_JOURNAL |
                                        (opt_verbose ? SRT_LOG_FLAGS_DEBUG : 0)),
                                       NULL, NULL, error))
    return FALSE;

  if (opt_quiet)
    g_log_set_handler (G_LOG_DOMAIN,
                       G_LOG_LEVEL_MESSAGE,
                       no_log_handler,
                       NULL);

  _srt_unblock_signals ();
  _srt_setenv_disable_gio_modules ();

  if (argc >= 2 && strcmp (argv[1], "--") == 0)
    {
      argv++;
      argc--;
    }

  if (argc > 2)
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                   "Usage: %s [OPTIONS] [DIRECTORY]", g_get_prgname ());
      return FALSE;
    }
  else if (argc == 2)
    {
      top = g_strdup (argv[1]);
    }
  else
    {
      const char *prefix = _srt_find_myself (NULL, error);

      if (prefix == NULL)
        return FALSE;

      top = g_path_get_dirname (prefix);
    }

  if (!glnx_opendirat (AT_FDCWD, top, TRUE, &top_fd, error))
    return FALSE;

  if (opt_mtree)
    mtree = g_strdup (opt_mtree);
  else if (opt_minimized_runtime)
    mtree = g_build_filename (top, "../usr-mtree.txt.gz", NULL);
  else
    mtree = g_build_filename (top, "mtree.txt.gz", NULL);

  if (opt_minimized_runtime)
    flags |= PV_MTREE_APPLY_FLAGS_MINIMIZED_RUNTIME;

  if (!pv_mtree_verify (mtree, top, top_fd, flags, error))
    return FALSE;

  return TRUE;
}

int
main (int argc,
      char *argv[])
{
  g_autoptr(GError) local_error = NULL;
  GError **error = &local_error;
  int ret = 0;

  if (run (argc, argv, error))
    goto out;

  g_return_val_if_fail (local_error != NULL, EX_SOFTWARE);
  _srt_log_failure ("%s", local_error->message);

  if (local_error->domain == G_OPTION_ERROR)
    ret = EX_USAGE;
  else
    ret = 1;

out:
  g_free (opt_mtree);
  return ret;
}
