/*
 * Copyright © 2021 Collabora Ltd.
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

/* Include it before steam-runtime-tools.h so that its backport of
 * G_DEFINE_AUTOPTR_CLEANUP_FUNC will be visible to it */
#include "steam-runtime-tools/glib-backports-internal.h"

#include <steam-runtime-tools/steam-runtime-tools.h>

#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "steam-runtime-tools/container-internal.h"
#include "steam-runtime-tools/system-info-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "test-utils.h"

static const char *argv0;
static gchar *global_sysroots;

typedef struct
{
  gchar *builddir;
  gchar *srcdir;
} Fixture;

typedef struct
{
  int unused;
} Config;

static void
setup (Fixture *f,
       gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  f->builddir = g_strdup (g_getenv ("G_TEST_BUILDDIR"));
  f->srcdir = g_strdup (g_getenv ("G_TEST_SRCDIR"));

  if (f->builddir == NULL)
    f->builddir = g_path_get_dirname (argv0);

  if (f->srcdir == NULL)
    f->srcdir = g_path_get_dirname (argv0);
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  g_free (f->builddir);
  g_free (f->srcdir);
}

/*
 * Test basic functionality of the SrtContainerInfo object.
 */
static void
test_object (Fixture *f,
             gconstpointer context)
{
  g_autoptr(SrtContainerInfo) container = NULL;
  g_autoptr(SrtOsInfo) host_os_info = NULL;
  SrtContainerType type;
  g_autofree gchar *flatpak_version = NULL;
  SrtFlatpakIssues flatpak_issues = SRT_FLATPAK_ISSUES_UNKNOWN;
  g_autofree gchar *host_directory = NULL;

  container = _srt_container_info_new (SRT_CONTAINER_TYPE_FLATPAK,
                                       SRT_FLATPAK_ISSUES_SUBSANDBOX_OUTPUT_CORRUPTED,
                                       "1.10.2",
                                       "/run/host",
                                       NULL);

  g_assert_cmpint (srt_container_info_get_container_type (container), ==,
                   SRT_CONTAINER_TYPE_FLATPAK);
  g_assert_cmpstr (srt_container_info_get_flatpak_version (container), ==, "1.10.2");
  g_assert_cmpuint (srt_container_info_get_flatpak_issues (container),
                    ==, SRT_FLATPAK_ISSUES_SUBSANDBOX_OUTPUT_CORRUPTED);
  g_assert_cmpstr (srt_container_info_get_container_host_directory (container), ==,
                   "/run/host");
  g_assert_null (srt_container_info_get_container_host_os_info (container));
  g_object_get (container,
                "type", &type,
                "flatpak-issues", &flatpak_issues,
                "flatpak-version", &flatpak_version,
                "host-directory", &host_directory,
                "host-os-info", &host_os_info,
                NULL);
  g_assert_cmpint (type, ==, SRT_CONTAINER_TYPE_FLATPAK);
  g_assert_cmpuint (flatpak_issues, ==, SRT_FLATPAK_ISSUES_SUBSANDBOX_OUTPUT_CORRUPTED);
  g_assert_cmpstr (flatpak_version, ==, "1.10.2");
  g_assert_cmpstr (host_directory, ==, "/run/host");
  g_assert_null (host_os_info);

  /* With a NULL runner, no helper programs are actually run, but we do
   * check the version number */
  _srt_container_info_check_issues (container, NULL);
  g_assert_cmpuint (srt_container_info_get_flatpak_issues (container),
                    ==, (SRT_FLATPAK_ISSUES_SUBSANDBOX_NOT_CHECKED
                         | SRT_FLATPAK_ISSUES_TOO_OLD));
}

typedef struct
{
  const char *description;
  const char *sysroot;
  SrtContainerType type;
  const char *host_directory;
  const char *flatpak_version;
  const char *host_os_id;
} ContainerTest;

static const ContainerTest container_tests[] =
{
  {
    .description = "Has /.dockerenv",
    .sysroot = "debian-unstable",
    .type = SRT_CONTAINER_TYPE_DOCKER,
  },
  {
    .description = "Has an unknown value in /run/systemd/container",
    .sysroot = "debian10",
    .type = SRT_CONTAINER_TYPE_UNKNOWN,
  },
  {
    .description = "Has 'docker' in /run/systemd/container",
    .sysroot = "fedora",
    .type = SRT_CONTAINER_TYPE_DOCKER,
  },
  {
    .description = "Has /.flatpak-info and /run/host",
    .sysroot = "flatpak-example",
    .type = SRT_CONTAINER_TYPE_FLATPAK,
    .host_directory = "/run/host",
    .flatpak_version = "1.14.0",
    .host_os_id = "debian",
  },
  {
    .description = "Has /run/host",
    .sysroot = "invalid-os-release",
    .type = SRT_CONTAINER_TYPE_UNKNOWN,
    .host_directory = "/run/host",
    .host_os_id = NULL,
  },
  {
    .description = "Has no evidence of being a container",
    .sysroot = "no-os-release",
    .type = SRT_CONTAINER_TYPE_NONE,
  },
  {
    .description = "Has /run/pressure-vessel",
    .sysroot = "steamrt",
    .type = SRT_CONTAINER_TYPE_PRESSURE_VESSEL,
  },
  {
    .description = "Has a Docker-looking /proc/1/cgroup",
    .sysroot = "steamrt-unofficial",
    .type = SRT_CONTAINER_TYPE_DOCKER,
  },
  {
    .description = "Has 'podman' in /run/host/container-manager",
    .sysroot = "podman-example",
    .type = SRT_CONTAINER_TYPE_PODMAN,
    .host_directory = "/run/host",
    .host_os_id = NULL,
  },
};

static void
test_containers (Fixture *f,
                 gconstpointer context)
{
  gsize i, j;

  for (i = 0; i < G_N_ELEMENTS (container_tests); i++)
    {
      const ContainerTest *test = &container_tests[i];
      g_autoptr(SrtSystemInfo) info;
      g_autofree gchar *expected_host = NULL;
      g_autofree gchar *sysroot = NULL;

      g_test_message ("%s: %s", test->sysroot, test->description);

      sysroot = g_build_filename (global_sysroots, test->sysroot, NULL);

      info = srt_system_info_new (NULL);
      g_assert_nonnull (info);
      srt_system_info_set_sysroot (info, sysroot);
      /* Skip the detailed check for Flatpak issues - we don't expect this
       * to pass when we're not really in a Flatpak app. This is tested
       * using mock steam-runtime-launch-client executables in
       * test_flatpak_issues(). */
      _srt_system_info_set_check_flags (info, SRT_CHECK_FLAGS_NO_HELPERS);

      if (test->host_directory == NULL)
        expected_host = NULL;
      else
        expected_host = g_build_filename (sysroot, test->host_directory, NULL);

      for (j = 0; j < 2; j++)
        {
          g_autoptr(SrtContainerInfo) container = NULL;
          g_autofree gchar *host_dir_dup = NULL;
          g_autoptr(SrtOsInfo) host_os_info_dup = NULL;
          SrtOsInfo *host_os_info = NULL;

          container = srt_system_info_check_container (info);
          g_assert_nonnull (container);

          g_assert_cmpint (srt_system_info_get_container_type (info), ==,
                           test->type);
          g_assert_cmpint (srt_container_info_get_container_type (container), ==,
                           test->type);

          if (test->type == SRT_CONTAINER_TYPE_FLATPAK)
            g_assert_cmpuint (srt_container_info_get_flatpak_issues (container),
                              ==, SRT_FLATPAK_ISSUES_SUBSANDBOX_NOT_CHECKED);
          else
            g_assert_cmpuint (srt_container_info_get_flatpak_issues (container),
                              ==, SRT_FLATPAK_ISSUES_NONE);

          host_os_info = srt_container_info_get_container_host_os_info (container);
          g_object_get (container,
                        "host-os-info", &host_os_info_dup,
                        NULL);
          g_assert_true (host_os_info == host_os_info_dup);

          if (test->host_directory != NULL)
            {
              g_assert_nonnull (host_os_info);
              g_assert_cmpstr (srt_os_info_get_id (host_os_info), ==, test->host_os_id);
            }
          else
            {
              g_assert_null (host_os_info);
            }

          host_dir_dup = srt_system_info_dup_container_host_directory (info);
          g_assert_cmpstr (host_dir_dup, ==, expected_host);
          g_assert_cmpstr (srt_container_info_get_container_host_directory (container),
                           ==, expected_host);

          g_assert_cmpstr (srt_container_info_get_flatpak_version (container),
                           ==, test->flatpak_version);
        }
    }
}

typedef struct
{
  const char *dir;
  SrtFlatpakIssues expected;
} FlatpakIssuesTest;

static const FlatpakIssuesTest flatpak_issues_tests[] =
{
  {
    .dir = "mock-flatpak/good",
    .expected = SRT_FLATPAK_ISSUES_NONE,
  },
  {
    .dir = "mock-flatpak/broken",
    .expected = SRT_FLATPAK_ISSUES_SUBSANDBOX_UNAVAILABLE,
  },
  {
    .dir = "mock-flatpak/no-display",
    .expected = SRT_FLATPAK_ISSUES_SUBSANDBOX_DID_NOT_INHERIT_DISPLAY,
  },
  {
    .dir = "mock-flatpak/suid",
    .expected = SRT_FLATPAK_ISSUES_SUBSANDBOX_LIMITED_BY_SETUID_BWRAP,
  },
  {
    .dir = "mock-flatpak/old",
    .expected = SRT_FLATPAK_ISSUES_TOO_OLD,
  },
  {
    .dir = "mock-flatpak/stdout",
    .expected = SRT_FLATPAK_ISSUES_SUBSANDBOX_OUTPUT_CORRUPTED,
  },
  {
    .dir = "mock-flatpak/timeout",
    .expected = SRT_FLATPAK_ISSUES_SUBSANDBOX_TIMED_OUT,
  },
};

static void
test_flatpak_issues (Fixture *f,
                     gconstpointer context)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(SrtContainerInfo) container = NULL;
  g_autoptr(SrtSysroot) sysroot = NULL;
  g_autofree gchar *sysroot_path = NULL;
  gsize i;

  sysroot_path = g_build_filename (global_sysroots, "flatpak-example", NULL);
  sysroot = _srt_sysroot_new (sysroot_path, &local_error);
  g_assert_no_error (local_error);
  g_assert_nonnull (sysroot);
  container = _srt_check_container (sysroot);
  g_assert_nonnull (container);

  /* Without calling _srt_container_info_check_issues(), we cannot know
   * whether there were any issues or not */
  g_assert_cmpuint (srt_container_info_get_flatpak_issues (container),
                    ==, SRT_FLATPAK_ISSUES_UNKNOWN);

  for (i = 0; i < G_N_ELEMENTS (flatpak_issues_tests); i++)
    {
      const FlatpakIssuesTest *test = &flatpak_issues_tests[i];
      g_autoptr(SrtSubprocessRunner) runner = NULL;
      g_autofree gchar *path = NULL;
      SrtTestFlags test_flags = SRT_TEST_FLAGS_NONE;

      if (test->expected & SRT_FLATPAK_ISSUES_SUBSANDBOX_TIMED_OUT)
        test_flags |= SRT_TEST_FLAGS_TIME_OUT_SOONER;

      /* Use a mock ${bindir} to present various results */
      path = g_build_filename (f->srcdir, test->dir, NULL);
      runner = _srt_subprocess_runner_new_full (_srt_const_strv (environ),
                                                path,
                                                NULL,
                                                test_flags);
      g_assert_nonnull (runner);
      _srt_container_info_check_issues (container, runner);
      g_assert_cmpuint (srt_container_info_get_flatpak_issues (container),
                        ==, test->expected);
    }
}

int
main (int argc,
      char **argv)
{
  argv0 = argv[0];
  _srt_tests_init (&argc, &argv, NULL);
  global_sysroots = _srt_global_setup_sysroots (argv0);

  g_test_add ("/container/object", Fixture, NULL,
              setup, test_object, teardown);
  g_test_add ("/container/containers", Fixture, NULL,
              setup, test_containers, teardown);
  g_test_add ("/container/flatpak-issues", Fixture, NULL,
              setup, test_flatpak_issues, teardown);

  return g_test_run ();
}
