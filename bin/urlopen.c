/*
 * Copyright © 2017 Red Hat, Inc
 * Copyright © 2021 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Florian Müllner <fmuellner@gnome.org>
 *       Matthias Clasen <mclasen@redhat.com>
 */

/*
 * Alternative executable to the canonical 'xdg-open' with a better handling
 * of Steam's URLs.
 * Loosely based on the xdg-open implementation of flatpak-xdg-utils.
 */

#include <libglnx.h>

#include "steam-runtime-tools/glib-backports-internal.h"

#include <glib.h>
#include <glib-object.h>
#include <gio/gunixfdlist.h>

#include <stdlib.h>
#include <steam-runtime-tools/container-internal.h>
#include <steam-runtime-tools/log-internal.h>
#include <steam-runtime-tools/runtime-internal.h>
#include <steam-runtime-tools/utils-internal.h>
#include <sys/wait.h>

#define THIS_PROGRAM "steam-runtime-urlopen"

typedef GUnixFDList AutoUnixFDList;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(AutoUnixFDList, g_object_unref)

static gchar **uris = NULL;
static gboolean opt_print_help = FALSE;
static gboolean opt_print_version = FALSE;

static const GOptionEntry option_entries[] =
{
  /* Imitate the allowed options of the real 'xdg-open' */
  { "manual", 0, G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, &opt_print_help,
    NULL, NULL },
  { "version", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_print_version,
    "Print version number and exit", NULL },
  { G_OPTION_REMAINING, 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME_ARRAY,
    &uris, NULL, NULL },
  { NULL }
};

static gboolean
open_with_portal (const char *uri_or_filename,
                  GError **error)
{
  g_autoptr(GDBusConnection) connection = NULL;

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);

  if (connection != NULL)
    {
      const gchar *portal_bus_name = "org.freedesktop.portal.Desktop";
      const gchar *portal_object_path = "/org/freedesktop/portal/desktop";
      const gchar *portal_iface_name = "org.freedesktop.portal.OpenURI";
      const gchar *method_name = NULL;
      GVariant *arguments = NULL;   /* floating */
      g_auto(GVariantBuilder) opt_builder = {};
      g_autoptr(AutoUnixFDList) fd_list = NULL;
      g_autoptr(GFile) file = NULL;
      g_autoptr(GVariant) result = NULL;

      g_debug ("Trying the D-Bus desktop portal");

      g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
      file = g_file_new_for_commandline_arg (uri_or_filename);

      if (g_file_is_native (file))
        {
          /* The canonical 'xdg-open' also handles paths. We try to replicate
           * that too, but it might not always work because the container
           * inside and outside filesystem structure might be different. */
          g_autofree gchar *path = NULL;
          int fd;

          path = g_file_get_path (file);
          fd = open (path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
          if (fd < 0)
            {
              return glnx_throw_errno_prefix (error, "Failed to open '%s'", path);
            }

          fd_list = g_unix_fd_list_new_from_array (&fd, 1); /* adopts the fd */

          arguments = g_variant_new ("(sh@a{sv})",
                                     "", 0,
                                     g_variant_builder_end (&opt_builder));
          method_name = "OpenFile";
        }
      else
        {
          arguments = g_variant_new ("(ss@a{sv})",
                                     "", uri_or_filename,
                                     g_variant_builder_end (&opt_builder));
          method_name = "OpenURI";
        }

      result = g_dbus_connection_call_with_unix_fd_list_sync (connection,
                                                              portal_bus_name,
                                                              portal_object_path,
                                                              portal_iface_name,
                                                              method_name,
                                                              /* sinks floating reference */
                                                              g_steal_pointer (&arguments),
                                                              NULL,
                                                              G_DBUS_CALL_FLAGS_NONE,
                                                              -1,
                                                              fd_list,
                                                              NULL,
                                                              NULL,
                                                              error);

      if (result == NULL)
        return glnx_prefix_error (error,
                                  "Unable to open URL with xdg-desktop-portal");
      else
        return TRUE;
    }
  else
    {
      return glnx_prefix_error (error,
                                "Unable to connect to D-Bus session bus");
    }
}

/*
 * Returns: %TRUE if running inside a LD_LIBRARY_PATH runtime
 */
static gboolean
is_ldlp_runtime (void)
{
  g_autoptr(SrtSysroot) sysroot = NULL;
  g_autoptr(SrtContainerInfo) container = NULL;
  g_autoptr(GError) error = NULL;
  const char *runtime;

  runtime = g_getenv ("STEAM_RUNTIME");
  if (runtime == NULL || runtime[0] != '/')
    return FALSE;

  sysroot = _srt_sysroot_new_direct (&error);
  if (sysroot == NULL)
    {
      g_warning ("_srt_sysroot_new_direct: %s", error->message);
      return FALSE;
    }

  container = _srt_check_container (sysroot);
  return srt_container_info_get_container_type (container)
    == SRT_CONTAINER_TYPE_NONE;
}

/*
 * prepare_xdg_open_if_available:
 * @out_exe: (out): Used to return a system xdg-open executable
 * @envpp: (inout): Environment in which to run `*out_exe`,
 *  which will be edited in-place to remove references to the Steam Runtime
 *  and the Steam overlay
 *
 * Returns: %TRUE if a suitable system copy of xdg-open was found
 */
static gboolean
prepare_xdg_open_if_available (char **out_exe,
                               GStrv *envpp,
                               GError **error)
{
  g_autofree char *exe = NULL;
  const char *search_path = NULL;

  g_return_val_if_fail (envpp != NULL, FALSE);
  *envpp = _srt_environ_escape_steam_runtime (*envpp,
                                              SRT_ESCAPE_RUNTIME_FLAGS_CLEAN_PATH);
  *envpp = g_environ_unsetenv (*envpp, "LD_PRELOAD");
  *envpp = g_environ_setenv (*envpp, _SRT_RECURSIVE_EXEC_GUARD_ENV, THIS_PROGRAM,
                             TRUE);

  search_path = g_environ_getenv (*envpp, "PATH");
  if (search_path == NULL)
    {
      search_path = "/usr/bin:/bin";
      g_warning ("$PATH is not set, defaulting to %s", search_path);
    }

  exe = _srt_find_next_executable (search_path, "xdg-open", error);
  if (exe == NULL)
    return FALSE;

  *out_exe = g_steal_pointer (&exe);

  return TRUE;
}

int
main (int argc,
      char **argv)
{
  const gchar *uri;
  g_autofree gchar *scheme = NULL;
  g_auto(GStrv) launch_environ = g_get_environ ();
  g_autoptr(GOptionContext) option_context = NULL;
  g_autoptr(GError) pipe_error = NULL;
  g_autoptr(GError) portal_error = NULL;
  g_autoptr(GError) xdg_open_error = NULL;
  g_autoptr(GError) error = NULL;
  gboolean prefer_steam;

  _srt_util_set_glib_log_handler (THIS_PROGRAM,
                                  G_LOG_DOMAIN,
                                  SRT_LOG_FLAGS_OPTIONALLY_JOURNAL,
                                  NULL, NULL, NULL);

  option_context = g_option_context_new ("{ file | URL }");
  g_option_context_add_main_entries (option_context, option_entries, NULL);

  if (!g_option_context_parse (option_context, &argc, &argv, &error))
    {
      g_printerr ("%s: %s\n", g_get_prgname (), error->message);
      return 1;
    }

  _srt_setenv_disable_gio_modules ();
  _srt_unblock_signals ();

  if (opt_print_version)
    {
      /* Simply print the version number, similarly to the real xdg-open */
      g_print ("%s\n", VERSION);
      return EXIT_SUCCESS;
    }

  if (uris == NULL || g_strv_length (uris) > 1)
    {
      g_autofree gchar *help = g_option_context_get_help (option_context, TRUE, NULL);
      g_print ("%s\n", help);
      return 1;
    }

  /* In reality this could also be a path, but we call it "uri" for simplicity */
  uri = uris[0];

  scheme = g_uri_parse_scheme (uri);

  /* For steam: and steamlink: URLs, we never want to go via
   * xdg-desktop-portal and the desktop environment's URL-handling
   * machinery, because there's a chance that they will choose the wrong
   * copy of Steam, for example if we have both native and Flatpak versions
   * of Steam installed. We want to use whichever one is actually running,
   * via the ~/.steam/steam.pipe in the current execution environment. */
  if (scheme != NULL && (g_ascii_strcasecmp (scheme, "steamlink") == 0
                         || g_ascii_strcasecmp (scheme, "steam") == 0))
    {
      g_debug ("Passing the URL '%s' to the Steam pipe", uri);
      if (_srt_steam_command_via_pipe (&uri, 1, &pipe_error))
        return EXIT_SUCCESS;
      else
        goto fail;
    }

  prefer_steam = _srt_boolean_environment ("SRT_URLOPEN_PREFER_STEAM", FALSE);

  if (!prefer_steam && open_with_portal (uri, &portal_error))
    return EXIT_SUCCESS;

  if (scheme != NULL && (g_ascii_strcasecmp (scheme, "http") == 0
                         || g_ascii_strcasecmp (scheme, "https") == 0))
    {
      g_autofree gchar *steam_url = NULL;
      steam_url = g_strjoin ("/", "steam://openurl_external", uri, NULL);

      g_debug ("Passing the URL '%s' to the Steam pipe", steam_url);
      if (_srt_steam_command_via_pipe ((const gchar **) &steam_url, 1, &pipe_error))
        return EXIT_SUCCESS;
    }

  /* If we haven't tried xdg-desktop-portal yet because we were hoping
   * to go via Steam, try it now - going by the less-preferred route is
   * better than nothing, and in particular we can't go via Steam for
   * non-web URLs like mailto: */
  if (portal_error == NULL && open_with_portal (uri, &portal_error))
    return EXIT_SUCCESS;

  /* As a last-ditch attempt, ask the host's xdg-open to open the URL instead. */
  if (is_ldlp_runtime ())
    {
      g_autofree char *xdg_open_exe = NULL;
      const char *xdg_open_argv[] = {"xdg-open", uri, NULL};

      if (!_srt_check_recursive_exec_guard ("xdg-open", &xdg_open_error))
        goto fail;

      if (!prepare_xdg_open_if_available (&xdg_open_exe,
                                          &launch_environ,
                                          &xdg_open_error))
        goto fail;

      if (pipe_error != NULL)
        g_printerr ("%s: tried using steam.pipe, received error: %s\n",
                    g_get_prgname (), pipe_error->message);

      if (portal_error != NULL)
        g_printerr ("%s: tried using xdg-desktop-portal, received error: %s\n",
                    g_get_prgname (), portal_error->message);

      /* Clear these out now to avoid double-printing the errors later */
      g_clear_error (&pipe_error);
      g_clear_error (&portal_error);

      g_printerr ("%s: trying xdg-open...\n", g_get_prgname ());

      execve (xdg_open_exe, (char *const *) xdg_open_argv, launch_environ);
      glnx_throw_errno_prefix (&xdg_open_error, "execve(%s)", xdg_open_exe);
    }

fail:
  g_printerr ("%s: Unable to open URL\n", g_get_prgname ());

  if (pipe_error != NULL)
    g_printerr ("%s: tried using steam.pipe, received error: %s\n",
                g_get_prgname (), pipe_error->message);

  if (portal_error != NULL)
    g_printerr ("%s: tried using xdg-desktop-portal, received error: %s\n",
                g_get_prgname (), portal_error->message);

  if (xdg_open_error != NULL)
    g_printerr ("%s: tried using xdg-open, received error: %s\n",
                g_get_prgname (), xdg_open_error->message);

  return 4;
}
