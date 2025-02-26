/*
 * steam-runtime-launch-client — send IPC requests to create child processes
 *
 * Copyright © 2018 Red Hat, Inc.
 * Copyright © 2020-2021 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Based on flatpak-spawn from the flatpak-xdg-utils package.
 * Copyright © 2018 Red Hat, Inc.
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#include "config.h"

#include <fnmatch.h>
#include <locale.h>
#include <stdlib.h>
#include <sysexits.h>
#include <sys/signalfd.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include "steam-runtime-tools/env-overlay-internal.h"
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/launcher-internal.h"
#include "steam-runtime-tools/log-internal.h"
#include "steam-runtime-tools/pty-bridge-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx.h"

#include "flatpak-portal.h"
#include "flatpak-session-helper.h"

typedef struct
{
  const char *service_iface;
  const char *service_obj_path;
  const char *service_bus_name;
  const char *send_signal_method;
  const char *exit_signal;
  const char *launch_method;
  guint clear_env_flag;
  gboolean default_dir_is_cwd;
} Api;

static Api launcher_api =
{
  .service_iface = LAUNCHER_IFACE,
  .service_obj_path = LAUNCHER_PATH,
  .service_bus_name = NULL,
  .send_signal_method = "SendSignal",
  .exit_signal = "ProcessExited",
  .launch_method = "Launch",
  .clear_env_flag = PV_LAUNCH_FLAGS_CLEAR_ENV,
  .default_dir_is_cwd = FALSE,
};

static const Api host_api =
{
  .service_iface = FLATPAK_SESSION_HELPER_INTERFACE_DEVELOPMENT,
  .service_obj_path = FLATPAK_SESSION_HELPER_PATH_DEVELOPMENT,
  .service_bus_name = FLATPAK_SESSION_HELPER_BUS_NAME,
  .send_signal_method = "HostCommandSignal",
  .exit_signal = "HostCommandExited",
  .launch_method = "HostCommand",
  .clear_env_flag = FLATPAK_HOST_COMMAND_FLAGS_CLEAR_ENV,
  .default_dir_is_cwd = TRUE,
};

static const Api subsandbox_api =
{
  .service_iface = FLATPAK_PORTAL_INTERFACE,
  .service_obj_path = FLATPAK_PORTAL_PATH,
  .service_bus_name = FLATPAK_PORTAL_BUS_NAME,
  .send_signal_method = "SpawnSignal",
  .exit_signal = "SpawnExited",
  .launch_method = "Spawn",
  .clear_env_flag = FLATPAK_SPAWN_FLAGS_CLEAR_ENV,
  .default_dir_is_cwd = TRUE,
};

static const Api *api = NULL;

typedef GUnixFDList AutoUnixFDList;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(AutoUnixFDList, g_object_unref)

static const char * const *global_original_environ = NULL;
static GDBusConnection *bus_or_peer_connection = NULL;
static guint child_pid = 0;
static int launch_exit_status = LAUNCH_EX_USAGE;
static SrtPtyBridge *first_pty_bridge = NULL;

static void
process_exited_cb (G_GNUC_UNUSED GDBusConnection *connection,
                   G_GNUC_UNUSED const gchar     *sender_name,
                   G_GNUC_UNUSED const gchar     *object_path,
                   G_GNUC_UNUSED const gchar     *interface_name,
                   G_GNUC_UNUSED const gchar     *signal_name,
                   GVariant                      *parameters,
                   gpointer                       user_data)
{
  GMainLoop *loop = user_data;
  guint32 client_pid = 0;
  guint32 wait_status = 0;

  if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(uu)")))
    return;

  g_variant_get (parameters, "(uu)", &client_pid, &wait_status);
  g_debug ("child %d exited: wait status %d", client_pid, wait_status);

  if (child_pid == client_pid)
    {
      int exit_code = 0;

      if (WIFEXITED (wait_status))
        {
          exit_code = WEXITSTATUS (wait_status);
        }
      else if (WIFSIGNALED (wait_status))
        {
          /* Smush the signal into an unsigned byte, as the shell does. This is
           * not quite right from the perspective of whatever ran flatpak-spawn
           * — it will get WIFEXITED() not WIFSIGNALED() — but the
           *  alternative is to disconnect all signal() handlers then send this
           *  signal to ourselves and hope it kills us.
           */
          exit_code = 128 + WTERMSIG (wait_status);
        }
      else
        {
          /* wait(3p) claims that if the waitpid() call that returned the exit
           * code specified neither WUNTRACED nor WIFSIGNALED, then exactly one
           * of WIFEXITED() or WIFSIGNALED() will be true.
           */
          g_warning ("wait status %d is neither WIFEXITED() nor WIFSIGNALED()",
                     wait_status);
          exit_code = LAUNCH_EX_CANNOT_REPORT;
        }

      g_debug ("child exit code %d: %d", client_pid, exit_code);
      launch_exit_status = exit_code;
      g_main_loop_quit (loop);
    }
}

static void
forward_signal (int sig)
{
  G_GNUC_UNUSED g_autoptr(GVariant) reply = NULL;
  gboolean to_process_group = FALSE;
  g_autoptr(GError) error = NULL;
  gboolean handled = FALSE;

  g_return_if_fail (api != NULL);

  if (first_pty_bridge != NULL)
    {
      if (!_srt_pty_bridge_handle_signal (first_pty_bridge, sig, &handled, &error))
        {
          g_debug ("%s", error->message);
          g_clear_error (&error);
        }
      else if (handled)
        {
          if (G_IN_SET (sig, SIGSTOP, SIGTSTP))
            {
              g_info ("SIGSTOP:ing myself");
              raise (SIGSTOP);
            }

          return;
        }
    }

  if (child_pid == 0)
    {
      /* We are not monitoring a child yet, so let the signal act on
       * this main process instead */
      if (sig == SIGTSTP || sig == SIGSTOP || sig == SIGTTIN || sig == SIGTTOU)
        {
          raise (SIGSTOP);
        }
      else if (sig != SIGCONT && sig != SIGWINCH)
        {
          sigset_t mask;

          sigemptyset (&mask);
          sigaddset (&mask, sig);
          /* Unblock it, so that it will be delivered properly this time.
           * Use pthread_sigmask instead of sigprocmask because the latter
           * has unspecified behaviour in a multi-threaded process. */
          pthread_sigmask (SIG_UNBLOCK, &mask, NULL);
          raise (sig);
        }

      return;
    }

  g_debug ("Forwarding signal: %d", sig);

  /* We forward stop requests as real stop, because the default doesn't
     seem to be to stop for non-kernel sent TSTP??? */
  if (sig == SIGTSTP)
    sig = SIGSTOP;

  /* ctrl-c/z is typically for the entire process group */
  if (sig == SIGINT || sig == SIGSTOP || sig == SIGCONT)
    to_process_group = TRUE;

  reply = g_dbus_connection_call_sync (bus_or_peer_connection,
                                       api->service_bus_name, /* NULL if p2p */
                                       api->service_obj_path,
                                       api->service_iface,
                                       api->send_signal_method,
                                       g_variant_new ("(uub)",
                                                      child_pid, sig,
                                                      to_process_group),
                                       G_VARIANT_TYPE ("()"),
                                       G_DBUS_CALL_FLAGS_NONE,
                                       -1, NULL, &error);

  if (error)
    g_info ("Failed to forward signal: %s", error->message);

  if (sig == SIGSTOP)
    {
      g_info ("SIGSTOP:ing myself");
      raise (SIGSTOP);
    }
}

static gboolean
forward_signal_handler (int sfd,
                        G_GNUC_UNUSED GIOCondition condition,
                        G_GNUC_UNUSED gpointer data)
{
  struct signalfd_siginfo info;
  ssize_t size;

  size = read (sfd, &info, sizeof (info));

  if (size < 0)
    {
      if (errno != EINTR && errno != EAGAIN)
        g_warning ("Unable to read struct signalfd_siginfo: %s",
                   g_strerror (errno));
    }
  else if (size != sizeof (info))
    {
      g_warning ("Expected struct signalfd_siginfo of size %"
                 G_GSIZE_FORMAT ", got %" G_GSSIZE_FORMAT,
                 sizeof (info), size);
    }
  else
    {
      forward_signal (info.ssi_signo);
    }

  return G_SOURCE_CONTINUE;
}

static guint
forward_signals (GError **error)
{
  static int forward[] = {
    SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGCONT, SIGTSTP, SIGUSR1, SIGUSR2,
    SIGTTIN, SIGTTOU, SIGWINCH,
  };
  sigset_t mask;
  guint i;
  int sfd;

  sigemptyset (&mask);

  for (i = 0; i < G_N_ELEMENTS (forward); i++)
    sigaddset (&mask, forward[i]);

  sfd = signalfd (-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);

  if (sfd < 0)
    {
      glnx_throw_errno_prefix (error, "Unable to watch signals");
      return 0;
    }

  /*
   * We have to block the signals, for two reasons:
   * - If we didn't, most of them would kill our process.
   *   Listening for a signal with a signalfd does not prevent the signal's
   *   default disposition from being acted on.
   * - Reading from a signalfd only returns information about the signals
   *   that are still pending for the process. If we ignored them instead
   *   of blocking them, they would no longer be pending by the time the
   *   main loop wakes up and reads from the signalfd.
   */
  if ((errno = pthread_sigmask (SIG_BLOCK, &mask, NULL)) != 0)
    {
      glnx_throw_errno_prefix (error, "Unable to block signals");
      return 0;
    }

  return g_unix_fd_add (sfd, G_IO_IN, forward_signal_handler, NULL);
}

static void
name_owner_changed (G_GNUC_UNUSED GDBusConnection *connection,
                    G_GNUC_UNUSED const gchar     *sender_name,
                    G_GNUC_UNUSED const gchar     *object_path,
                    G_GNUC_UNUSED const gchar     *interface_name,
                    G_GNUC_UNUSED const gchar     *signal_name,
                    GVariant                      *parameters,
                    G_GNUC_UNUSED gpointer         user_data)
{
  GMainLoop *loop = user_data;
  const char *name, *from, *to;

  g_return_if_fail (api != NULL);
  g_return_if_fail (api->service_bus_name != NULL);

  g_variant_get (parameters, "(&s&s&s)", &name, &from, &to);

  /* Check if the service dies, then we exit, because we can't track it anymore */
  if (strcmp (name, api->service_bus_name) == 0 &&
      strcmp (to, "") == 0)
    {
      g_debug ("portal exited");

      if (child_pid == 0)
        launch_exit_status = LAUNCH_EX_FAILED;
      else
        launch_exit_status = LAUNCH_EX_CANNOT_REPORT;

      g_main_loop_quit (loop);
    }
}

static void
connection_closed_cb (G_GNUC_UNUSED GDBusConnection *conn,
                      G_GNUC_UNUSED gboolean remote_peer_vanished,
                      G_GNUC_UNUSED GError *error,
                      GMainLoop *loop)
{
  g_debug ("D-Bus connection closed, quitting");

  if (child_pid == 0)
    launch_exit_status = LAUNCH_EX_FAILED;
  else
    launch_exit_status = LAUNCH_EX_CANNOT_REPORT;

  g_main_loop_quit (loop);
}

static guint32
get_portal_version (void)
{
  static guint32 version = 0;

  g_return_val_if_fail (api != NULL, 0);
  g_return_val_if_fail (api == &host_api || api == &subsandbox_api, 0);

  if (version == 0)
    {
      g_autoptr(GError) error = NULL;
      g_autoptr(GVariant) reply =
        g_dbus_connection_call_sync (bus_or_peer_connection,
                                     api->service_bus_name,
                                     api->service_obj_path,
                                     "org.freedesktop.DBus.Properties",
                                     "Get",
                                     g_variant_new ("(ss)", api->service_iface, "version"),
                                     G_VARIANT_TYPE ("(v)"),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL, &error);

      if (reply == NULL)
        g_debug ("Failed to get version: %s", error->message);
      else
        {
          g_autoptr(GVariant) v = g_variant_get_child_value (reply, 0);
          g_autoptr(GVariant) v2 = g_variant_get_variant (v);
          version = g_variant_get_uint32 (v2);
        }
    }

  return version;
}

static void
check_portal_version (const char *option, guint32 version_needed)
{
  guint32 portal_version = get_portal_version ();
  if (portal_version < version_needed)
    {
      g_printerr ("--%s not supported by host portal version (need version %d, has %d)\n", option, version_needed, portal_version);
      exit (1);
    }
}

static guint32
get_portal_supports (void)
{
  static guint32 supports = 0;
  static gboolean ran = FALSE;

  g_return_val_if_fail (api != NULL, 0);
  g_return_val_if_fail (api == &host_api || api == &subsandbox_api, 0);

  if (!ran)
    {
      g_autoptr(GError) error = NULL;
      g_autoptr(GVariant) reply = NULL;

      ran = TRUE;

      /* Support flags were added in version 3 */
      if (get_portal_version () >= 3)
        {
          reply = g_dbus_connection_call_sync (bus_or_peer_connection,
                                               api->service_bus_name,
                                               api->service_obj_path,
                                               "org.freedesktop.DBus.Properties",
                                               "Get",
                                               g_variant_new ("(ss)", api->service_iface, "supports"),
                                               G_VARIANT_TYPE ("(v)"),
                                               G_DBUS_CALL_FLAGS_NONE,
                                               -1,
                                               NULL, &error);
          if (reply == NULL)
            g_debug ("Failed to get supports: %s", error->message);
          else
            {
              g_autoptr(GVariant) v = g_variant_get_child_value (reply, 0);
              g_autoptr(GVariant) v2 = g_variant_get_variant (v);
              supports = g_variant_get_uint32 (v2);
            }
        }
    }

  return supports;
}

#define NOT_SETUID_ROOT_MESSAGE \
"This feature requires Flatpak to be using a bubblewrap (bwrap) executable\n" \
"that is not setuid root.\n" \
"\n" \
"The non-setuid version of bubblewrap requires a kernel that allows\n" \
"unprivileged users to create new user namespaces.\n" \
"\n" \
"For more details please see:\n" \
"https://github.com/flatpak/flatpak/wiki/User-namespace-requirements\n" \
"\n"

static void
check_portal_supports (const char *option, guint32 supports_needed)
{
  guint32 supports = get_portal_supports ();

  if ((supports & supports_needed) != supports_needed)
    {
      g_printerr ("--%s not supported by host portal\n", option);

      if (supports_needed == FLATPAK_SPAWN_SUPPORT_FLAGS_EXPOSE_PIDS)
        g_printerr ("\n%s", NOT_SETUID_ROOT_MESSAGE);

      exit (1);
    }
}

static gint32
path_to_handle (GUnixFDList *fd_list,
                const char *path,
                const char *home_realpath,
                const char *flatpak_id,
                GError **error)
{
  int path_fd = open (path, O_PATH|O_CLOEXEC|O_NOFOLLOW|O_RDONLY);
  int saved_errno;
  gint32 handle;

  if (path_fd < 0)
    {
      saved_errno = errno;
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved_errno),
                   "Failed to open %s to expose in sandbox: %s",
                   path, g_strerror (saved_errno));
      return -1;
    }

  if (home_realpath != NULL && flatpak_id != NULL)
    {
      g_autofree char *real = NULL;
      const char *after = NULL;

      real = realpath (path, NULL);

      if (real != NULL)
        after = _srt_get_path_after (real, home_realpath);

      if (after != NULL)
        {
          g_autofree char *var_path = NULL;
          int var_fd = -1;
          struct stat path_buf;
          struct stat var_buf;

          /* @after is possibly "", but that's OK: if @path is exactly $HOME,
           * we want to check whether it's the same file as
           * ~/.var/app/$FLATPAK_ID, with no suffix */
          var_path = g_build_filename (home_realpath, ".var", "app", flatpak_id,
                                       after, NULL);

          var_fd = open (var_path, O_PATH|O_CLOEXEC|O_NOFOLLOW|O_RDONLY);

          if (var_fd >= 0 &&
              fstat (path_fd, &path_buf) == 0 &&
              fstat (var_fd, &var_buf) == 0 &&
              path_buf.st_dev == var_buf.st_dev &&
              path_buf.st_ino == var_buf.st_ino)
            {
              close (path_fd);
              path_fd = var_fd;
            }
          else
            {
              close (var_fd);
            }
        }
    }


  handle = g_unix_fd_list_append (fd_list, path_fd, error);

  if (handle < 0)
    {
      g_prefix_error (error, "Failed to add fd to list for %s: ", path);
      return -1;
    }

  /* The GUnixFdList keeps a duplicate, so we should release the original */
  close (path_fd);
  return handle;
}

static int
list_servers (FILE *original_stdout,
              GError **error)
{
  static const char * const flatpak_names[] =
  {
    FLATPAK_PORTAL_BUS_NAME,
    FLATPAK_SESSION_HELPER_BUS_NAME,
  };
  g_autoptr(GDBusConnection) session_bus = NULL;
  g_autoptr(GVariant) reply = NULL;
  g_auto(GStrv) running = NULL;
  g_auto(GStrv) activatable = NULL;
  gsize i;

  session_bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);

  if (session_bus == NULL)
    {
      glnx_prefix_error (error, "Can't find session bus");
      return LAUNCH_EX_FAILED;
    }

  reply = g_dbus_connection_call_sync (session_bus,
                                       DBUS_NAME_DBUS,
                                       DBUS_PATH_DBUS,
                                       DBUS_INTERFACE_DBUS,
                                       "ListNames",
                                       NULL,
                                       G_VARIANT_TYPE ("(as)"),
                                       G_DBUS_CALL_FLAGS_NONE,
                                       -1, NULL, error);

  if (reply == NULL)
    return LAUNCH_EX_FAILED;

  g_variant_get (reply, "(^as)", &running);

  g_clear_pointer (&reply, g_variant_unref);
  reply = g_dbus_connection_call_sync (session_bus,
                                       DBUS_NAME_DBUS,
                                       DBUS_PATH_DBUS,
                                       DBUS_INTERFACE_DBUS,
                                       "ListActivatableNames",
                                       NULL,
                                       G_VARIANT_TYPE ("(as)"),
                                       G_DBUS_CALL_FLAGS_NONE,
                                       -1, NULL, error);

  if (reply == NULL)
    return LAUNCH_EX_FAILED;

  g_variant_get (reply, "(^as)", &activatable);

  qsort (running, g_strv_length (running), sizeof (char *),
         _srt_indirect_strcmp0);

  for (i = 0; running[i] != NULL; i++)
    {
      if (g_str_has_prefix (running[i], LAUNCHER_INSIDE_APP_PREFIX)
          || g_str_equal (running[i], LAUNCHER_NAME_ALONGSIDE_STEAM))
        fprintf (original_stdout, "--bus-name=%s\n", running[i]);
    }

  for (i = 0; i < G_N_ELEMENTS (flatpak_names); i++)
    {
      if (g_strv_contains ((const char * const *) running, flatpak_names[i])
          || g_strv_contains ((const char * const *) activatable, flatpak_names[i]))
        fprintf (original_stdout, "--bus-name=%s\n", flatpak_names[i]);
    }

  return 0;
}

/*
 * @session_bus_p: (out) (not optional):
 * @service_bus_name_p: (out) (not optional):
 */
static const Api *
choose_implementation (GPtrArray *possible_names,
                       GDBusConnection **session_bus_p,
                       gchar **service_bus_name_p,
                       GError **error)
{
  gsize i;

  if (possible_names->len == 0)
    {
      g_assert (launcher_api.service_bus_name == NULL);
      return &launcher_api;
    }

  for (i = 0; i < possible_names->len; i++)
    {
      const char *name = g_ptr_array_index (possible_names, i);
      g_autoptr(GError) local_error = NULL;
      g_autoptr(GVariant) reply = NULL;

      /* Do this inside the loop, so that if no bus names were specified
       * (in which case we'll be using a peer-to-peer socket),
       * it isn't an error to have no session bus. */
      if (*session_bus_p == NULL)
        *session_bus_p = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);

      if (*session_bus_p == NULL)
        {
          glnx_prefix_error (error, "Can't find session bus");
          return NULL;
        }

      if (g_str_equal (name, host_api.service_bus_name)
          || g_str_equal (name, subsandbox_api.service_bus_name))
        {
          /* The Flatpak services are stateless and might be
           * service-activatable */
          reply = g_dbus_connection_call_sync (*session_bus_p,
                                               name,
                                               "/",
                                               DBUS_INTERFACE_PEER,
                                               "Ping",
                                               NULL,
                                               G_VARIANT_TYPE ("()"),
                                               G_DBUS_CALL_FLAGS_NONE,
                                               -1, NULL, &local_error);

          if (reply != NULL)
            {
              if (g_str_equal (name, host_api.service_bus_name))
                {
                  g_info ("Connected to flatpak-session-helper: %s", name);
                  return &host_api;
                }
              else
                {
                  g_info ("Connected to flatpak-portal: %s", name);
                  return &subsandbox_api;
                }
            }
        }
      else
        {
          /* steam-runtime-launcher-service is stateful, so we want to bind
           * to a specific unique bus name (specific instance) and expect signals
           * from there.
           *
           * It also isn't service-activatable, so we don't need to worry about
           * whether it is already running or whether it needs to be
           * service-activated: we know that service activation is going to fail
           * in any case.
           *
           * We can do this even for unique names: the owner of a unique name
           * is itself. */
          reply = g_dbus_connection_call_sync (*session_bus_p,
                                               DBUS_NAME_DBUS,
                                               DBUS_PATH_DBUS,
                                               DBUS_INTERFACE_DBUS,
                                               "GetNameOwner",
                                               g_variant_new ("(s)", name),
                                               G_VARIANT_TYPE ("(s)"),
                                               G_DBUS_CALL_FLAGS_NONE,
                                               -1, NULL, &local_error);

          if (reply != NULL)
            {
              g_variant_get (reply, "(s)", service_bus_name_p);
              launcher_api.service_bus_name = *service_bus_name_p;
              g_info ("Connected to steam-runtime-launcher-service: %s (%s)",
                      name, launcher_api.service_bus_name);
              return &launcher_api;
            }
        }

      g_dbus_error_strip_remote_error (local_error);
      g_info ("Unable to connect to %s: %s", name, local_error->message);
    }

  glnx_throw (error,
              "Unable to connect to any of the specified bus names");
  return FALSE;
}

static SrtEnvOverlay *global_env_overlay = NULL;
static gchar **forward_fds = NULL;
static gchar *opt_app_path = NULL;
static GPtrArray *opt_bus_names = NULL;
static gboolean opt_clear_env = FALSE;
static gchar *opt_dbus_address = NULL;
static gchar *opt_directory = NULL;
static gboolean opt_list = FALSE;
static gchar *opt_shell_command = NULL;
static gchar *opt_socket = NULL;
static gboolean opt_share_pids = FALSE;
static gboolean opt_terminate = FALSE;
static gchar *opt_usr_path = NULL;
static gboolean opt_verbose = FALSE;
static gboolean opt_version = FALSE;

static gboolean
opt_pass_env_cb (const char *option_name,
                 const gchar *value,
                 G_GNUC_UNUSED gpointer data,
                 GError **error)
{
  if (g_strcmp0 (option_name, "--pass-env") == 0)
    return _srt_env_overlay_pass_cli (global_env_overlay,
                                      option_name,
                                      value,
                                      global_original_environ,
                                      error);

  if (g_strcmp0 (option_name, "--pass-env-matching") == 0)
    return _srt_env_overlay_pass_matching_pattern_cli (global_env_overlay,
                                                       option_name,
                                                       value,
                                                       global_original_environ,
                                                       error);

  g_return_val_if_reached (FALSE);
}

static gboolean
opt_bus_name_cb (G_GNUC_UNUSED const gchar *option_name,
                 const gchar *value,
                 G_GNUC_UNUSED gpointer data,
                 GError **error)
{
  g_assert (opt_bus_names != NULL);

  if (!g_dbus_is_name (value))
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                   "\"%s\" is not a valid D-Bus name", value);
      return FALSE;
    }

  g_ptr_array_add (opt_bus_names, g_strdup (value));
  return TRUE;
}

static gboolean
opt_host_cb (G_GNUC_UNUSED const gchar *option_name,
             G_GNUC_UNUSED const gchar *value,
             G_GNUC_UNUSED gpointer data,
             G_GNUC_UNUSED GError **error)
{
  g_assert (opt_bus_names != NULL);

  launcher_api.default_dir_is_cwd = TRUE;

  /* There is currently no conventional name for a s-r-l-s process on the
   * host system, so --host is effectively syntactic sugar for talking
   * to flatpak-session-helper */
  g_ptr_array_add (opt_bus_names,
                   g_strdup (FLATPAK_SESSION_HELPER_BUS_NAME));
  return TRUE;
}

static gboolean
opt_inside_cb (G_GNUC_UNUSED const gchar *option_name,
               const gchar *value,
               G_GNUC_UNUSED gpointer data,
               GError **error)
{
  g_assert (opt_bus_names != NULL);

  g_ptr_array_add (opt_bus_names,
                   g_strdup_printf ("%s%s",
                                    LAUNCHER_INSIDE_APP_PREFIX, value));
  return TRUE;
}

static gboolean
opt_alongside_steam_cb (G_GNUC_UNUSED const gchar *option_name,
                        G_GNUC_UNUSED const gchar *value,
                        G_GNUC_UNUSED gpointer data,
                        GError **error)
{
  const char *bus_name = g_getenv ("SRT_LAUNCHER_SERVICE_ALONGSIDE_STEAM");
  struct stat stat_buf;

  g_assert (opt_bus_names != NULL);

  launcher_api.default_dir_is_cwd = TRUE;

  if (bus_name != NULL && bus_name[0] != '\0')
    g_ptr_array_add (opt_bus_names, g_strdup (bus_name));

  g_ptr_array_add (opt_bus_names, g_strdup (LAUNCHER_NAME_ALONGSIDE_STEAM));

  /* In a Flatpak environment, launching a new subsandbox might be the
   * closest we can get to launching alongside Steam */
  if (stat ("/.flatpak-info", &stat_buf) == 0
      && g_strcmp0 (g_getenv ("FLATPAK_ID"), "com.valvesoftware.Steam") == 0)
    g_ptr_array_add (opt_bus_names, g_strdup (FLATPAK_PORTAL_BUS_NAME));

  return TRUE;
}

static const GOptionEntry options[] =
{
  { "app-path", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_app_path,
    "Use DIR as the /app for a Flatpak sub-sandbox. "
    "Requires '--bus-name=org.freedesktop.portal.Flatpak'.",
    "DIR" },
  { "alongside-steam", '\0',
    G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_alongside_steam_cb,
    "Connect to a service running alongside the Steam client, outside the "
    "container for the current Steam app.",
    NULL },
  { "bus-name", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, opt_bus_name_cb,
    "Connect to a Launcher service with this name on the session bus.",
    "NAME" },
  { "dbus-address", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &opt_dbus_address,
    "Connect to a Launcher server listening on this D-Bus address.",
    "ADDRESS" },
  { "clear-env", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_clear_env,
    "Run with clean environment.", NULL },
  { "directory", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_directory,
    "Working directory in which to run the command.", "DIR" },
  { "forward-fd", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING_ARRAY, &forward_fds,
    "Connect a file descriptor to the launched process. "
    "fds 0, 1 and 2 are automatically forwarded.",
    "FD" },
  { "host", '\0',
    G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_host_cb,
    "Connect to a service running on the host system.",
    NULL },
  { "inside-app", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, opt_inside_cb,
    "Connect to a service running inside the container for the given Steam app.",
    "APPID" },
  { "list", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_list,
    "List some of the available servers and then exit.",
    NULL },
  { "pass-env", '\0',
    G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, opt_pass_env_cb,
    "Pass environment variable through, or unset if set.", "VAR" },
  { "pass-env-matching", '\0',
    G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, opt_pass_env_cb,
    "Pass environment variables matching a shell-style wildcard.",
    "WILDCARD" },
  { "share-pids", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_share_pids,
    "Use same pid namespace as calling sandbox.", NULL },
  { "shell-command", 'c',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &opt_shell_command,
    "Run this command via /bin/sh, with COMMAND as $0 and ARG... as $@.",
    "SHELL_COMMAND" },
  { "usr-path", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_usr_path,
    "Use DIR as the /usr for a Flatpak sub-sandbox. "
    "Requires '--bus-name=org.freedesktop.portal.Flatpak'.",
    "DIR" },
  { "socket", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &opt_socket,
    "Connect to a Launcher server listening on this AF_UNIX socket.",
    "ABSPATH|@ABSTRACT" },
  { "terminate", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_terminate,
    "Terminate the Launcher server after the COMMAND (if any) has run.",
    NULL },
  { "verbose", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_verbose,
    "Be more verbose.", NULL },
  { "version", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_version,
    "Print version number and exit.", NULL },
  { NULL }
};

int
main (int argc,
      char *argv[])
{
  g_auto(GStrv) original_environ = NULL;
  g_autoptr(GMainLoop) loop = NULL;
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GPtrArray) replacement_command_and_args = NULL;
  g_autoptr(GPtrArray) shell_argv = NULL;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(SrtEnvOverlay) env_overlay = NULL;
  GError **error = &local_error;
  char **command_and_args;
  g_autoptr(FILE) original_stdout = NULL;
  glnx_autofd int original_stdout_fd = -1;
  g_autoptr(GDBusConnection) session_bus = NULL;
  g_autoptr(GDBusConnection) peer_connection = NULL;
  g_auto(GVariantBuilder) fd_builder = {};
  g_auto(GVariantBuilder) env_builder = {};
  g_auto(GVariantBuilder) options_builder = {};
  g_autoptr(AutoUnixFDList) fd_list = NULL;
  g_autoptr(GHashTable) pty_bridges = NULL;
  G_GNUC_UNUSED g_autoptr(GVariant) reply = NULL;
  gint stdin_handle = -1;
  gint stdout_handle = -1;
  gint stderr_handle = -1;
  guint spawn_flags = 0;
  guint signal_source = 0;
  gsize i;
  GHashTableIter iter;
  gpointer key, value;
  g_autofree char *home_realpath = NULL;
  g_autofree char *service_bus_name = NULL;
  const char *flatpak_id = NULL;
  gboolean unsetenv = FALSE;
  static const char * const run_interactive_shell[] =
  {
    "sh",
    "-euc",
    ("if [ -n \"${SHELL-}\" ]; then\n"
     "  if command -v \"$SHELL\" >/dev/null; then\n"
     "    exec \"$SHELL\"\n"
     "  fi\n"
     "  echo \"Shell '$SHELL' not available, falling back to bash\" >&2\n"
     "fi\n"
     "if command -v bash >/dev/null; then\n"
     "  exec bash\n"
     "fi\n"
     "echo 'bash not available, falling back to sh' >&2\n"
     "exec sh"),
    NULL
  };

  setlocale (LC_ALL, "");

  original_environ = g_get_environ ();
  global_original_environ = (const char * const *) original_environ;

  /* Set up the initial base logging */
  if (!_srt_util_set_glib_log_handler ("steam-runtime-launch-client",
                                       G_LOG_DOMAIN,
                                       SRT_LOG_FLAGS_DIVERT_STDOUT,
                                       &original_stdout_fd, NULL, error))
    {
      launch_exit_status = LAUNCH_EX_FAILED;
      goto out;
    }

  original_stdout = fdopen (original_stdout_fd, "w");

  if (original_stdout == NULL)
    {
      glnx_throw_errno_prefix (error,
                               "Unable to create a stdio wrapper for fd %d",
                               original_stdout_fd);
      launch_exit_status = LAUNCH_EX_FAILED;
      goto out;
    }
  else
    {
      original_stdout_fd = -1;    /* ownership taken, do not close */
    }

  context = g_option_context_new ("COMMAND [ARG...]");
  g_option_context_set_summary (context,
                                "Send IPC requests to create child "
                                "processes.");
  g_option_context_add_main_entries (context, options, NULL);

  env_overlay = _srt_env_overlay_new ();
  global_env_overlay = env_overlay;
  g_option_context_add_group (context,
                              _srt_env_overlay_create_option_group (env_overlay));

  /* Guess we might need up to 4 names + NULL, which is enough for
   * "--alongside-steam --host" without reallocation */
  opt_bus_names = g_ptr_array_new_full (5, g_free);
  opt_verbose = _srt_boolean_environment ("PRESSURE_VESSEL_VERBOSE", FALSE);

  for (i = 0; i <= 2; i++)
    {
      if (isatty (i))
        {
          g_debug ("Passing through TERM environment variable because "
                   "fd %zu is a terminal", i);
          _srt_env_overlay_set (env_overlay, "TERM", g_getenv ("TERM"));
          break;
        }
    }

  if (!g_option_context_parse (context, &argc, &argv, error))
    goto out;

  if (opt_version)
    {
      fprintf (original_stdout,
               "%s:\n"
               " Package: pressure-vessel\n"
               " Version: %s\n",
               g_get_prgname (), VERSION);
      launch_exit_status = 0;
      goto out;
    }

  if (!_srt_util_set_glib_log_handler (NULL, G_LOG_DOMAIN,
                                       (opt_verbose ? SRT_LOG_FLAGS_DEBUG : 0),
                                       NULL, NULL, error))
    {
      launch_exit_status = LAUNCH_EX_FAILED;
      goto out;
    }

  /* Must be before forward_signals() which partially undoes this */
  _srt_unblock_signals ();

  if (opt_list)
    {
      launch_exit_status = list_servers (original_stdout, error);
      goto out;
    }

  if (argc > 1)
    {
      /* We have to block the signals we want to forward before we start any
       * other thread, and in particular the GDBus worker thread, because
       * the signal mask is per-thread. We need all threads to have the same
       * mask, otherwise a thread that doesn't have the mask will receive
       * process-directed signals, causing the whole process to exit. */
      signal_source = forward_signals (error);

      if (signal_source == 0)
        {
          launch_exit_status = LAUNCH_EX_FAILED;
          goto out;
        }
    }

  _srt_setenv_disable_gio_modules ();

  flatpak_id = g_environ_getenv (original_environ, "FLATPAK_ID");

  if (flatpak_id != NULL)
    home_realpath = realpath (g_get_home_dir (), NULL);

  if (opt_bus_names->len > 0 && opt_socket != NULL)
    {
      glnx_throw (error, "--bus-name and --socket cannot both be used");
      goto out;
    }

  api = choose_implementation (opt_bus_names,
                               &session_bus, &service_bus_name, error);

  if (api == NULL)
    goto out;

  if (api != &launcher_api && opt_terminate)
    {
      glnx_throw (error,
                  "--terminate cannot be used with Flatpak services");
      goto out;
    }

  if (api != &subsandbox_api && opt_app_path != NULL)
    {
      glnx_throw (error,
                  "--app-path can only be used with a Flatpak subsandbox");
      goto out;
    }

  if (api != &subsandbox_api && opt_usr_path != NULL)
    {
      glnx_throw (error,
                  "--usr-path can only be used with a Flatpak subsandbox");
      goto out;
    }

  if (argc >= 2 && strcmp (argv[1], "--") == 0)
    {
      argv++;
      argc--;
    }

  if (opt_shell_command)
    {
      shell_argv = g_ptr_array_new_with_free_func (g_free);

      g_ptr_array_add (shell_argv, g_strdup ("sh"));
      g_ptr_array_add (shell_argv, g_strdup ("-c"));
      g_ptr_array_add (shell_argv, g_strdup (opt_shell_command));

      for (i = 1; i < (gsize) argc; i++)
        g_ptr_array_add (shell_argv, g_strdup (argv[i]));

      g_ptr_array_add (shell_argv, NULL);
      command_and_args = (char **) shell_argv->pdata;
    }
  else if (argc < 2)
    {
      if (opt_terminate)
        command_and_args = NULL;
      else
        command_and_args = (char **) run_interactive_shell;
    }
  else
    {
      command_and_args = argv + 1;
    }

  launch_exit_status = LAUNCH_EX_FAILED;
  loop = g_main_loop_new (NULL, FALSE);

  if (api->service_bus_name != NULL)
    {
      if (opt_dbus_address != NULL || opt_socket != NULL)
        {
          glnx_throw (error,
                      "--bus-name cannot be combined with "
                      "--dbus-address or --socket");
          launch_exit_status = LAUNCH_EX_USAGE;
          goto out;
        }

      /* choose_implementation() already connected */
      g_assert (session_bus != NULL);
      bus_or_peer_connection = session_bus;
    }
  else if (opt_dbus_address != NULL)
    {
      if (opt_socket != NULL)
        {
          glnx_throw (error,
                      "--dbus-address cannot be combined with --socket");
          launch_exit_status = LAUNCH_EX_USAGE;
          goto out;
        }

      _srt_log_warning ("The --dbus-address option is deprecated. Prefer to use the session bus.");
      peer_connection = g_dbus_connection_new_for_address_sync (opt_dbus_address,
                                                                G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                                                                NULL,
                                                                NULL,
                                                                error);
      if (peer_connection == NULL)
        {
          glnx_prefix_error (error, "Can't connect to peer address");
          goto out;
        }

      bus_or_peer_connection = peer_connection;
    }
  else if (opt_socket != NULL)
    {
      g_autofree gchar *address = NULL;
      g_autofree gchar *escaped = NULL;

      if (opt_socket[0] == '@')
        {
          escaped = g_dbus_address_escape_value (&opt_socket[1]);
          address = g_strdup_printf ("unix:abstract=%s", escaped);
        }
      else if (opt_socket[0] == '/')
        {
          escaped = g_dbus_address_escape_value (opt_socket);
          address = g_strdup_printf ("unix:path=%s", escaped);
        }
      else
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Invalid socket address '%s'", opt_socket);
          goto out;
        }

      _srt_log_warning ("The --socket option is deprecated. Prefer to use the session bus.");
      peer_connection = g_dbus_connection_new_for_address_sync (address,
                                                                G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                                                                NULL,
                                                                NULL,
                                                                error);
      if (peer_connection == NULL)
        {
          glnx_prefix_error (error, "Can't connect to peer socket");
          goto out;
        }

      bus_or_peer_connection = peer_connection;
    }
  else
    {
      glnx_throw (error, "At least one of --host, --inside-app, --alongside-steam, --bus-name, --dbus-address or --socket is required");
      launch_exit_status = LAUNCH_EX_USAGE;
      goto out;
    }

  g_assert (bus_or_peer_connection != NULL);

  if (command_and_args == NULL)
    {
      g_assert (opt_terminate);   /* already checked */

      reply = g_dbus_connection_call_sync (bus_or_peer_connection,
                                           api->service_bus_name,
                                           api->service_obj_path,
                                           api->service_iface,
                                           "Terminate",
                                           g_variant_new ("()"),
                                           G_VARIANT_TYPE ("()"),
                                           G_DBUS_CALL_FLAGS_NONE,
                                           -1,
                                           NULL, error);

      if (local_error != NULL)
        g_dbus_error_strip_remote_error (local_error);

      if (reply != NULL)
        launch_exit_status = 0;

      goto out;
    }

  g_assert (command_and_args != NULL);
  g_dbus_connection_signal_subscribe (bus_or_peer_connection,
                                      api->service_bus_name,  /* NULL if p2p */
                                      api->service_iface,
                                      api->exit_signal,
                                      api->service_obj_path,
                                      NULL,
                                      G_DBUS_SIGNAL_FLAGS_NONE,
                                      process_exited_cb,
                                      g_main_loop_ref (loop),
                                      (GDestroyNotify) g_main_loop_unref);

  g_variant_builder_init (&fd_builder, G_VARIANT_TYPE ("a{uh}"));
  g_variant_builder_init (&env_builder, G_VARIANT_TYPE ("a{ss}"));
  fd_list = g_unix_fd_list_new ();

  stdin_handle = g_unix_fd_list_append (fd_list, 0, error);
  if (stdin_handle < 0)
    {
      glnx_prefix_error (error, "Can't append fd 0");
      goto out;
    }
  /* Remember that our stdout is now a copy of our original stderr,
   * so we need to bypass that and use our *original* stdout here. */
  stdout_handle = g_unix_fd_list_append (fd_list,
                                         fileno (original_stdout),
                                         error);
  if (stdout_handle < 0)
    {
      glnx_prefix_error (error, "Can't append fd 1");
      goto out;
    }
  stderr_handle = g_unix_fd_list_append (fd_list, 2, error);
  if (stderr_handle < 0)
    {
      glnx_prefix_error (error, "Can't append fd 2");
      goto out;
    }

  g_variant_builder_add (&fd_builder, "{uh}", 0, stdin_handle);
  g_variant_builder_add (&fd_builder, "{uh}", 1, stdout_handle);
  g_variant_builder_add (&fd_builder, "{uh}", 2, stderr_handle);

  for (i = 0; forward_fds != NULL && forward_fds[i] != NULL; i++)
    {
      int fd = strtol (forward_fds[i],  NULL, 10);
      gint handle = -1;

      if (fd == 0)
        {
          glnx_throw (error, "Invalid fd '%s'", forward_fds[i]);
          goto out;
        }

      if (fd >= 0 && fd <= 2)
        continue; // We always forward these

      handle = g_unix_fd_list_append (fd_list, fd, error);
      if (handle == -1)
        {
          glnx_prefix_error (error, "Can't append fd");
          goto out;
        }
      /* The GUnixFdList keeps a duplicate, so we should release the original */
      close (fd);
      g_variant_builder_add (&fd_builder, "{uh}", fd, handle);
    }

  g_hash_table_iter_init (&iter, env_overlay->values);

  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      if (value != NULL)
        g_variant_builder_add (&env_builder, "{ss}", key, value);
      else
        unsetenv = TRUE;
    }

  spawn_flags = 0;

  if (opt_clear_env)
    spawn_flags |= api->clear_env_flag;

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE ("a{sv}"));

  if (opt_app_path != NULL)
    {
      gint32 handle;

      g_debug ("Using \"%s\" as /app instead of runtime", opt_app_path);

      g_assert (api == &subsandbox_api);
      check_portal_version ("app-path", 6);

      if (opt_app_path[0] == '\0')
        {
          /* Empty path is special-cased to mean an empty directory */
          spawn_flags |= FLATPAK_SPAWN_FLAGS_EMPTY_APP;
        }
      else
        {
          handle = path_to_handle (fd_list, opt_app_path, home_realpath,
                                   flatpak_id, error);

          if (handle < 0)
            goto out;

          g_variant_builder_add (&options_builder, "{s@v}", "app-fd",
                                 g_variant_new_variant (g_variant_new_handle (handle)));
        }
    }

  if (opt_usr_path != NULL)
    {
      gint32 handle;

      g_debug ("Using %s as /usr instead of runtime", opt_usr_path);

      g_assert (api == &subsandbox_api);
      check_portal_version ("usr-path", 6);

      handle = path_to_handle (fd_list, opt_usr_path, home_realpath,
                               flatpak_id, error);

      if (handle < 0)
        goto out;

      g_variant_builder_add (&options_builder, "{s@v}", "usr-fd",
                             g_variant_new_variant (g_variant_new_handle (handle)));
    }

  if (opt_terminate)
    {
      g_assert (api == &launcher_api);
      g_variant_builder_add (&options_builder, "{s@v}", "terminate-after",
                             g_variant_new_variant (g_variant_new_boolean (TRUE)));
    }

  /* We just ignore this option when not using a subsandbox:
   * host_api and launcher_api always share process IDs anyway */
  if (opt_share_pids && api == &subsandbox_api)
    {
      check_portal_version ("share-pids", 5);
      check_portal_supports ("share-pids", FLATPAK_SPAWN_SUPPORT_FLAGS_SHARE_PIDS);

      spawn_flags |= FLATPAK_SPAWN_FLAGS_SHARE_PIDS;
    }

  if (unsetenv)
    {
      g_hash_table_iter_init (&iter, env_overlay->values);

      /* The host portal doesn't support options, so we always have to do
       * this the hard way. The subsandbox portal supports unset-env in
       * versions >= 5. steam-runtime-launcher-service always supports it. */
      if (api == &launcher_api
          || (api == &subsandbox_api && get_portal_version () >= 5))
        {
          GVariantBuilder strv_builder;

          g_variant_builder_init (&strv_builder, G_VARIANT_TYPE_STRING_ARRAY);

          while (g_hash_table_iter_next (&iter, &key, &value))
            {
              if (value == NULL)
                g_variant_builder_add (&strv_builder, "s", key);
            }

          g_variant_builder_add (&options_builder, "{s@v}", "unset-env",
                                 g_variant_new_variant (g_variant_builder_end (&strv_builder)));
        }
      else
        {
          replacement_command_and_args = g_ptr_array_new_with_free_func (g_free);

          g_ptr_array_add (replacement_command_and_args, g_strdup ("/usr/bin/env"));

          while (g_hash_table_iter_next (&iter, &key, &value))
            {
              if (value == NULL)
                {
                  g_ptr_array_add (replacement_command_and_args, g_strdup ("-u"));
                  g_ptr_array_add (replacement_command_and_args, g_strdup (key));
                }
            }

          if (strchr (command_and_args[0], '=') != NULL)
            {
              g_ptr_array_add (replacement_command_and_args, g_strdup ("/bin/sh"));
              g_ptr_array_add (replacement_command_and_args, g_strdup ("-euc"));
              g_ptr_array_add (replacement_command_and_args, g_strdup ("exec \"$@\""));
              g_ptr_array_add (replacement_command_and_args, g_strdup ("sh"));  /* argv[0] */
            }

          for (i = 0; command_and_args[i] != NULL; i++)
            g_ptr_array_add (replacement_command_and_args, g_strdup (command_and_args[i]));

          g_ptr_array_add (replacement_command_and_args, NULL);
          command_and_args = (char **) replacement_command_and_args->pdata;
        }
    }

  if (!opt_directory)
    {
      if (api->default_dir_is_cwd)
        opt_directory = g_get_current_dir ();
      else
        opt_directory = g_strdup ("");
    }

  if (session_bus != NULL)
    g_dbus_connection_signal_subscribe (session_bus,
                                        DBUS_NAME_DBUS,
                                        DBUS_INTERFACE_DBUS,
                                        "NameOwnerChanged",
                                        DBUS_PATH_DBUS,
                                        NULL,
                                        G_DBUS_SIGNAL_FLAGS_NONE,
                                        name_owner_changed,
                                        g_main_loop_ref (loop),
                                        (GDestroyNotify) g_main_loop_unref);

  {
    g_autoptr(GVariant) fds = NULL;
    g_autoptr(GVariant) env = NULL;
    g_autoptr(GVariant) opts = NULL;
    GVariant *arguments = NULL;   /* floating */
    int fd_list_len;
    const int *fd_arr = g_unix_fd_list_peek_fds (fd_list, &fd_list_len);

    g_assert (fd_list_len >= 0);
    pty_bridges = g_hash_table_new_full (_srt_struct_stat_devino_hash,
                                         _srt_struct_stat_devino_equal,
                                         g_free, g_object_unref);

    for (i = 0; i < (gsize) fd_list_len; i++)
      {
        int fd = fd_arr[i];
        struct stat stat_buf = {};
        SrtPtyBridge *pty_bridge;   /* (unowned) */

        if (!isatty (fd))
          continue;

        if (fstat (fd, &stat_buf) != 0)
          {
            glnx_throw_errno_prefix (error, "Unable to inspect terminal fd %d", fd);
            goto out;
          }

        pty_bridge = g_hash_table_lookup (pty_bridges, &stat_buf);

        if (pty_bridge == NULL)
          {
            g_autoptr(SrtPtyBridge) new_bridge = NULL;
            int dest_fd = fd;
            const char *in_desc = "input";
            const char *out_desc = "output";

            /* If stdin is a terminal, see whether stdout and/or stderr
             * point to the same terminal. If yes, then we can use a
             * single bridge to handle both directions. */
            if (i == stdin_handle)
              {
                struct stat other_stat = {};

                in_desc = "copy of stdin";

                if (fstat (fd_arr[stdout_handle], &other_stat) == 0
                    && _srt_is_same_stat (&stat_buf, &other_stat))
                  {
                    dest_fd = fd_arr[stdout_handle];
                    out_desc = "copy of stdout";
                  }
                else if (fstat (fd_arr[stderr_handle], &other_stat) == 0
                         && _srt_is_same_stat (&stat_buf, &other_stat))
                  {
                    dest_fd = fd_arr[stderr_handle];
                    out_desc = "copy of stderr";
                  }
              }

            g_debug ("Creating new pseudo-terminal bridge for fd %d (%s), %d (%s)",
                     fd, in_desc, dest_fd, out_desc);
            new_bridge = _srt_pty_bridge_new (fd, dest_fd, error);

            if (new_bridge == NULL)
              {
                glnx_throw_errno_prefix (error, "Unable to set up forwarding for terminal");
                goto out;
              }

            if (first_pty_bridge == NULL)
              first_pty_bridge = g_object_ref (new_bridge);

            pty_bridge = new_bridge;
            g_hash_table_replace (pty_bridges,
                                  g_memdup2 (&stat_buf, sizeof (struct stat)),
                                  g_steal_pointer (&new_bridge));
          }
        else
          {
            g_debug ("Reusing existing pseudo-terminal bridge for fd %d", fd);
          }

        /* Change the meaning of the fd that is stored in the fd list
         * (a duplicate of the original fd) to be the fd of the terminal
         * end of the bridge, so that the launched process can take it
         * as its controlling terminal if it wants to. */
        if (dup3 (_srt_pty_bridge_get_terminal_fd (pty_bridge), fd, O_CLOEXEC) < 0)
          {
            glnx_throw_errno_prefix (error, "Unable to duplicate terminal fd");
            goto out;
          }
      }

    /* Close the terminal end of each ptmx/terminal pair, now that the
     * fd list has been populated with duplicates of them to be sent to
     * the launcher service */
    g_hash_table_iter_init (&iter, pty_bridges);
    while (g_hash_table_iter_next (&iter, NULL, &value))
      _srt_pty_bridge_close_terminal_fd (value);

    g_debug ("Forwarding command:");

    for (i = 0; command_and_args[i] != NULL; i++)
      g_debug ("\t%s", command_and_args[i]);

    fds = g_variant_ref_sink (g_variant_builder_end (&fd_builder));
    env = g_variant_ref_sink (g_variant_builder_end (&env_builder));
    opts = g_variant_ref_sink (g_variant_builder_end (&options_builder));

    if (api == &host_api)
      {
        /* o.fd.Flatpak.Development doesn't take arbitrary options a{sv} */
        arguments = g_variant_new ("(^ay^aay@a{uh}@a{ss}u)",
                                   opt_directory,
                                   (const char * const *) command_and_args,
                                   fds,
                                   env,
                                   spawn_flags);
      }
    else
      {
        arguments = g_variant_new ("(^ay^aay@a{uh}@a{ss}u@a{sv})",
                                   opt_directory,
                                   (const char * const *) command_and_args,
                                   fds,
                                   env,
                                   spawn_flags,
                                   opts);
      }

    /* It's important that we didn't append any more fds after replacing
     * terminal references with pseudoterminals */
    g_assert (fd_list_len == g_unix_fd_list_get_length (fd_list));

    reply = g_dbus_connection_call_with_unix_fd_list_sync (bus_or_peer_connection,
                                                           api->service_bus_name,
                                                           api->service_obj_path,
                                                           api->service_iface,
                                                           api->launch_method,
                                                           /* sinks floating reference */
                                                           g_steal_pointer (&arguments),
                                                           G_VARIANT_TYPE ("(u)"),
                                                           G_DBUS_CALL_FLAGS_NONE,
                                                           -1,
                                                           fd_list,
                                                           NULL,
                                                           NULL, error);

    if (reply == NULL)
      {
        g_dbus_error_strip_remote_error (local_error);
        goto out;
      }

    g_variant_get (reply, "(u)", &child_pid);
  }

  g_debug ("child_pid: %d", child_pid);

  /* Release our reference to the fds, so that only the copy we sent over
   * D-Bus remains open */
  g_clear_object (&fd_list);

  g_signal_connect (bus_or_peer_connection, "closed",
                    G_CALLBACK (connection_closed_cb), loop);

  g_main_loop_run (loop);

out:

  if (local_error != NULL)
    _srt_log_failure ("%s", local_error->message);

  if (signal_source > 0)
    g_source_remove (signal_source);

  g_strfreev (forward_fds);
  g_free (opt_app_path);
  g_free (opt_shell_command);
  g_free (opt_directory);
  g_free (opt_socket);
  g_ptr_array_unref (opt_bus_names);
  g_free (opt_usr_path);
  global_env_overlay = NULL;
  global_original_environ = NULL;
  g_clear_object (&first_pty_bridge);

  g_debug ("Exiting with status %d", launch_exit_status);
  return launch_exit_status;
}
