/*
 * Copyright © 2020 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Perform some checks to ensure that the Steam client requirements are met.
 * Output a human-readable message on stdout if the current system does not
 * meet every requirement.
 */

#include <steam-runtime-tools/glib-backports-internal.h>

#include <steam-runtime-tools/steam-runtime-tools.h>

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <glib.h>
#include <glib-object.h>

#include <steam-runtime-tools/bwrap-internal.h>
#include <steam-runtime-tools/container-internal.h>
#include <steam-runtime-tools/cpu-feature-internal.h>
#include <steam-runtime-tools/log-internal.h>
#include <steam-runtime-tools/resolve-in-sysroot-internal.h>
#include <steam-runtime-tools/steam-internal.h>
#include <steam-runtime-tools/utils-internal.h>

#define X86_FEATURES_REQUIRED (SRT_X86_FEATURE_X86_64 \
                               | SRT_X86_FEATURE_CMPXCHG16B \
                               | SRT_X86_FEATURE_SSE3)

enum
{
  OPTION_HELP = 1,
  OPTION_VERSION,
  OPTION_VERBOSE = 'v',
};

struct option long_options[] =
{
    { "verbose", no_argument, NULL, OPTION_VERBOSE },
    { "version", no_argument, NULL, OPTION_VERSION },
    { "help", no_argument, NULL, OPTION_HELP },
    { NULL, 0, NULL, 0 }
};

static void usage (int code) __attribute__((__noreturn__));

/*
 * Print usage information and exit with status @code.
 */
static void
usage (int code)
{
  FILE *fp;

  if (code == 0)
    fp = stdout;
  else
    fp = stderr;

  fprintf (fp, "Usage: %s [OPTIONS]\n",
           program_invocation_short_name);
  exit (code);
}

static gboolean
check_x86_features (SrtX86FeatureFlags features)
{
  return ((features & X86_FEATURES_REQUIRED) == X86_FEATURES_REQUIRED);
}

static const char cannot_run_bwrap[] =
"Steam now requires user namespaces to be enabled.\n"
"\n"
"If the file /proc/sys/kernel/unprivileged_userns_clone exists, check that\n"
"it contains value 1.\n"
"\n"
"If the file /proc/sys/user/max_user_namespaces exists, check that its\n"
"value is at least 100.\n"
"\n"
"This requirement is the same as for Flatpak, which has more detailed\n"
"information available:\n"
"https://github.com/flatpak/flatpak/wiki/User-namespace-requirements\n"
;

static const char installed_in_usr[] =
"Steam is intended to install into your home directory, typically\n"
"~/.local/share/Steam. It cannot be installed below /usr.\n"
;

static const char flatpak_needs_unprivileged_bwrap[] =
"The unofficial Steam Flatpak app now requires user namespaces to be\n"
"enabled.\n"
"\n"
"Check that the bubblewrap executable used by Flatpak, usually\n"
"/usr/bin/bwrap or /usr/libexec/flatpak-bwrap, is not setuid root.\n"
"\n"
"If the file /proc/sys/kernel/unprivileged_userns_clone exists, check that\n"
"it contains value 1.\n"
"\n"
"If the file /proc/sys/user/max_user_namespaces exists, check that its\n"
"value is at least 100.\n"
"\n"
"For more details, please see:\n"
"https://github.com/flatpak/flatpak/wiki/User-namespace-requirements\n"
;

static const char flatpak_too_old[] =
"The unofficial Steam Flatpak app requires Flatpak 1.12.0 or later.\n"
"Using the latest stable release of Flatpak is recommended.\n"
;

static const char flatpak_needs_display[] =
"The unofficial Steam Flatpak app requires a correctly-configured desktop\n"
"session, which must provide the DISPLAY environment variable to the\n"
"D-Bus session bus activation environment.\n"
"\n"
"On systems that use systemd --user, the DISPLAY environment variable must\n"
"also be present in the systemd --user activation environment.\n"
"\n"
"This is usually achieved by running:\n"
"\n"
"    dbus-update-activation-environment DISPLAY\n"
"\n"
"during desktop environment startup.\n"
"\n"
"For more details, please see:\n"
"https://github.com/ValveSoftware/steam-for-linux/issues/10554\n"
;

/* This one is the generic "something went wrong" message for Flatpak,
 * so we can't be particularly specific here. */
static const char flatpak_needs_subsandbox[] =
"The unofficial Steam Flatpak app requires a working D-Bus session bus\n"
"and flatpak-portal service.\n"
"\n"
"Running this command might provide more diagnostic information:\n"
"\n"
"    flatpak run --command=bash com.valvesoftware.Steam -c 'flatpak-spawn -vv true'\n"
;

int
main (int argc,
      char **argv)
{
  g_autoptr(FILE) original_stdout = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(SrtContainerInfo) container_info = NULL;
  g_autoptr(SrtSubprocessRunner) runner = NULL;
  g_autoptr(SrtSysroot) sysroot = NULL;
  SrtContainerType container_type;
  glnx_autofd int original_stdout_fd = -1;
  SrtX86FeatureFlags x86_features = SRT_X86_FEATURE_NONE;
  SrtX86FeatureFlags known = SRT_X86_FEATURE_NONE;
  SrtSteamIssues steam_issues;
  const gchar *output = NULL;
  const char *prefix = NULL;
  const char *pkglibexecdir = NULL;
  int opt;
  int exit_code = EXIT_SUCCESS;
  SrtLogFlags log_flags;
  SrtFlatpakIssues flatpak_issues;

  _srt_setenv_disable_gio_modules ();
  log_flags = SRT_LOG_FLAGS_OPTIONALLY_JOURNAL | SRT_LOG_FLAGS_DIVERT_STDOUT;

  while ((opt = getopt_long (argc, argv, "v", long_options, NULL)) != -1)
    {
      switch (opt)
        {
          case OPTION_VERBOSE:
            if (log_flags & SRT_LOG_FLAGS_INFO)
              log_flags |= SRT_LOG_FLAGS_DEBUG;
            else
              log_flags |= SRT_LOG_FLAGS_INFO;
            break;

          case OPTION_VERSION:
            /* Output version number as YAML for machine-readability,
             * inspired by `ostree --version` and `docker version` */
            printf (
                "%s:\n"
                " Package: steam-runtime-tools\n"
                " Version: %s\n",
                argv[0], VERSION);
            return EXIT_SUCCESS;

          case OPTION_HELP:
            usage (0);
            break;

          case '?':
          default:
            usage (EX_USAGE);
            break;  /* not reached */
        }
    }

  if (optind != argc)
    usage (EX_USAGE);

  if (!_srt_util_set_glib_log_handler ("steam-runtime-check-requirements",
                                       G_LOG_DOMAIN, log_flags,
                                       &original_stdout_fd, NULL, &error))
    {
      g_warning ("%s", error->message);
      g_clear_error (&error);
      return EXIT_FAILURE;
    }

  original_stdout = fdopen (original_stdout_fd, "w");

  if (original_stdout == NULL)
    {
      g_warning ("Unable to create a stdio wrapper for fd %d: %s",
                 original_stdout_fd, g_strerror (errno));
      return EXIT_FAILURE;
    }
  else
    {
      original_stdout_fd = -1;    /* ownership taken, do not close */
    }

  _srt_unblock_signals ();

  x86_features = _srt_feature_get_x86_flags (NULL, &known);

  if (!check_x86_features (x86_features))
    {
      output = "Sorry, this computer's CPU is too old to run Steam.\n"
               "\nSteam requires at least an Intel Pentium 4 or AMD Opteron, with the following features:\n"
               "\t- x86-64 (AMD64) instruction set (lm in /proc/cpuinfo flags)\n"
               "\t- CMPXCHG16B instruction support (cx16 in /proc/cpuinfo flags)\n"
               "\t- SSE3 instruction support (pni in /proc/cpuinfo flags)\n";
      exit_code = EX_OSERR;
      goto out;
    }

  prefix = _srt_find_myself (NULL, &pkglibexecdir, &error);

  if (prefix == NULL)
    {
      g_warning ("Internal error: %s", error->message);
      g_clear_error (&error);
    }

  sysroot = _srt_sysroot_new_direct (&error);

  if (sysroot == NULL)
    {
      g_warning ("Internal error: %s", error->message);
      g_clear_error (&error);
      container_type = SRT_CONTAINER_TYPE_UNKNOWN;
    }
  else
    {
      container_info = _srt_check_container (sysroot);
      container_type = srt_container_info_get_container_type (container_info);
    }

  switch (container_type)
    {
      case SRT_CONTAINER_TYPE_PRESSURE_VESSEL:
        g_info ("Already under pressure-vessel, not checking bwrap functionality.");
        break;

      case SRT_CONTAINER_TYPE_FLATPAK:
        runner = _srt_subprocess_runner_new ();
        _srt_container_info_check_issues (container_info, runner);
        flatpak_issues = srt_container_info_get_flatpak_issues (container_info);

        if (flatpak_issues & SRT_FLATPAK_ISSUES_SUBSANDBOX_LIMITED_BY_SETUID_BWRAP)
          {
            output = flatpak_needs_unprivileged_bwrap;
            exit_code = EX_OSERR;
            goto out;
          }

        if (flatpak_issues & SRT_FLATPAK_ISSUES_TOO_OLD)
          {
            output = flatpak_too_old;
            exit_code = EX_OSERR;
            goto out;
          }

        if (flatpak_issues & SRT_FLATPAK_ISSUES_SUBSANDBOX_DID_NOT_INHERIT_DISPLAY)
          {
            output = flatpak_needs_display;
            exit_code = EX_OSERR;
            goto out;
          }

        if (flatpak_issues & (SRT_FLATPAK_ISSUES_SUBSANDBOX_UNAVAILABLE
                              | SRT_FLATPAK_ISSUES_SUBSANDBOX_TIMED_OUT))
          {
            output = flatpak_needs_subsandbox;
            exit_code = EX_OSERR;
            goto out;
          }

        break;

      case SRT_CONTAINER_TYPE_DOCKER:
      case SRT_CONTAINER_TYPE_PODMAN:
      case SRT_CONTAINER_TYPE_SNAP:
      case SRT_CONTAINER_TYPE_UNKNOWN:
      case SRT_CONTAINER_TYPE_NONE:
      default:
        if (pkglibexecdir != NULL)
          {
            g_autofree gchar *bwrap = _srt_check_bwrap (pkglibexecdir, FALSE, NULL);

            if (bwrap == NULL)
              {
                output = cannot_run_bwrap;
                exit_code = EX_OSERR;
               goto out;
              }

            g_info ("Found working bwrap executable at %s", bwrap);
          }
       else
          {
            _srt_log_warning ("Unable to locate srt-bwrap, not checking "
                              "functionality.");
          }
     }

  steam_issues = _srt_steam_check (_srt_const_strv (environ),
                                   ~SRT_STEAM_ISSUES_DESKTOP_FILE_RELATED,
                                   NULL);

  if (steam_issues & SRT_STEAM_ISSUES_INSTALLED_IN_USR)
    {
      output = installed_in_usr;
      exit_code = EX_OSERR;
      goto out;
    }

  g_info ("No problems detected");

out:
  if (output != NULL)
    {
      if (fputs (output, original_stdout) < 0)
        g_warning ("Unable to write output: %s", g_strerror (errno));

      if (fputs ("\n", original_stdout) < 0)
        g_warning ("Unable to write final newline: %s", g_strerror (errno));
    }

  if (fclose (g_steal_pointer (&original_stdout)) != 0)
    g_warning ("Unable to close stdout: %s", g_strerror (errno));

  return exit_code;
}
