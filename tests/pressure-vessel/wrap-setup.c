/*
 * Copyright © 2019-2021 Collabora Ltd.
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

#include <stdlib.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx.h"

#include "tests/test-utils.h"

#include "bwrap.h"
#include "supported-architectures.h"
#include "wrap-home.h"
#include "wrap-setup.h"
#include "utils.h"

#define MOCK_ABI "mock-multiarch-tuple"

/* These match the first entry in PvMultiArchdetails.platforms,
 * which is the easiest realistic thing for a mock implementation of
 * srt_system_info_check_library() to use. */
#define MOCK_PLATFORM_32 "i686"
#define MOCK_PLATFORM_64 "xeon_phi"
#define MOCK_PLATFORM_GENERIC "mock"

/* These match Debian multiarch, which is as good a thing as any for
 * a mock implementation of srt_system_info_check_library() to use. */
#define MOCK_LIB_32 "lib/" SRT_ABI_I386
#define MOCK_LIB_64 "lib/" SRT_ABI_X86_64
#define MOCK_LIB_GENERIC "lib/" MOCK_ABI

typedef struct
{
  TestsOpenFdSet old_fds;
  SrtSysroot *mock_host;
  FlatpakBwrap *bwrap;
  gchar *tmpdir;
  gchar *mock_runtime;
  gchar *var;
  GStrv env;
  int tmpdir_fd;
  int mock_runtime_fd;
  int var_fd;
} Fixture;

typedef struct
{
  PvRuntimeFlags runtime_flags;
} Config;

static const Config default_config = {};
static const Config copy_config =
{
  .runtime_flags = PV_RUNTIME_FLAGS_COPY_RUNTIME,
};
static const Config interpreter_root_config =
{
  .runtime_flags = (PV_RUNTIME_FLAGS_COPY_RUNTIME
                    | PV_RUNTIME_FLAGS_INTERPRETER_ROOT),
};

static int
open_or_die (const char *path,
             int flags,
             int mode)
{
  glnx_autofd int fd = open (path, flags | O_CLOEXEC, mode);

  if (fd >= 0)
    return g_steal_fd (&fd);
  else
    g_error ("open(%s, 0x%x): %s", path, flags, g_strerror (errno));
}

/*
 * Populate root_fd with the given directories and symlinks.
 * The paths use a simple domain-specific language:
 * - symlinks are given as "link>target"
 * - directories are given as "dir/"
 * - any other string is created as a regular 0-byte file
 */
static void
fixture_populate_dir (Fixture *f,
                      int root_fd,
                      const char * const *paths,
                      gsize n_paths)
{
  g_autoptr(GError) local_error = NULL;
  gsize i;

  for (i = 0; i < n_paths; i++)
    {
      if (strchr (paths[i], '>'))
        {
          g_auto(GStrv) pieces = g_strsplit (paths[i], ">", 2);

          g_test_message ("Creating symlink %s -> %s", pieces[0], pieces[1]);
          g_assert_no_errno (TEMP_FAILURE_RETRY (symlinkat (pieces[1], root_fd, pieces[0])));
        }
      else if (g_str_has_suffix (paths[i], "/"))
        {
          g_test_message ("Creating directory %s", paths[i]);

          glnx_shutil_mkdir_p_at (root_fd, paths[i], 0755, NULL, &local_error);
          g_assert_no_error (local_error);
        }
      else
        {
          g_autofree char *dir = g_path_get_dirname (paths[i]);

          g_test_message ("Creating directory %s", dir);
          glnx_shutil_mkdir_p_at (root_fd, dir, 0755, NULL, &local_error);
          g_assert_no_error (local_error);

          g_test_message ("Creating file %s", paths[i]);
          glnx_file_replace_contents_at (root_fd, paths[i],
                                         (const guint8 *) "", 0, 0, NULL,
                                         &local_error);
          g_assert_no_error (local_error);
        }
    }
}

static FlatpakExports *
fixture_create_exports (Fixture *f)
{
  g_autoptr(FlatpakExports) exports = flatpak_exports_new ();
  glnx_autofd int fd = open_or_die (f->mock_host->path, O_RDONLY | O_DIRECTORY, 0755);

  flatpak_exports_take_host_fd (exports, g_steal_fd (&fd));
  return g_steal_pointer (&exports);
}

static PvRuntime *
fixture_create_runtime (Fixture *f,
                        PvRuntimeFlags flags)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(PvGraphicsProvider) graphics_provider = NULL;
  g_autoptr(PvRuntime) runtime = NULL;
  const char *gfx_in_container;

  if (flags & PV_RUNTIME_FLAGS_FLATPAK_SUBSANDBOX)
    gfx_in_container = "/run/parent";
  else
    gfx_in_container = "/run/host";

  graphics_provider = pv_graphics_provider_new ("/", gfx_in_container,
                                                TRUE, &local_error);
  g_assert_no_error (local_error);
  g_assert_nonnull (graphics_provider);

  runtime = pv_runtime_new (f->mock_runtime,
                            f->var,
                            NULL,
                            graphics_provider,
                            NULL,
                            _srt_peek_environ_nonnull (),
                            (flags
                             | PV_RUNTIME_FLAGS_VERBOSE
                             | PV_RUNTIME_FLAGS_SINGLE_THREAD),
                            &local_error);
  g_assert_no_error (local_error);
  g_assert_nonnull (runtime);
  return g_steal_pointer (&runtime);
}

static void
setup (Fixture *f,
       gconstpointer context)
{
  g_autoptr(GError) local_error = NULL;
  g_autofree gchar *mock_host = NULL;

  f->old_fds = tests_check_fd_leaks_enter ();
  f->tmpdir = g_dir_make_tmp ("pressure-vessel-tests.XXXXXX", &local_error);
  g_assert_no_error (local_error);
  glnx_opendirat (AT_FDCWD, f->tmpdir, TRUE, &f->tmpdir_fd, &local_error);
  g_assert_no_error (local_error);

  mock_host = g_build_filename (f->tmpdir, "host", NULL);
  f->mock_runtime = g_build_filename (f->tmpdir, "runtime", NULL);
  f->var = g_build_filename (f->tmpdir, "var", NULL);
  g_assert_no_errno (g_mkdir (mock_host, 0755));
  g_assert_no_errno (g_mkdir (f->mock_runtime, 0755));
  g_assert_no_errno (g_mkdir (f->var, 0755));
  f->mock_host = _srt_sysroot_new (mock_host, &local_error);
  g_assert_no_error (local_error);
  glnx_opendirat (AT_FDCWD, f->mock_runtime, TRUE, &f->mock_runtime_fd, &local_error);
  g_assert_no_error (local_error);
  glnx_opendirat (AT_FDCWD, f->var, TRUE, &f->var_fd, &local_error);
  g_assert_no_error (local_error);

  f->bwrap = flatpak_bwrap_new (flatpak_bwrap_empty_env);
  f->env = g_get_environ ();
}

static void
setup_ld_preload (Fixture *f,
                  gconstpointer context)
{
  static const char * const touch[] =
  {
    "app/lib/libpreloadA.so",
    "future/libs-post2038/.exists",
    "home/me/libpreloadH.so",
    "lib/libpreload-rootfs.so",
    "overlay/libs/usr/lib/libpreloadO.so",
    "steam/lib/gameoverlayrenderer.so",
    "usr/lib/libpreloadU.so",
    "usr/local/lib/libgtk3-nocsd.so.0",
#if defined(__i386__) || defined(__x86_64__)
    "opt/" MOCK_LIB_32 "/libpreloadL.so",
    "opt/" MOCK_LIB_64 "/libpreloadL.so",
    "platform/plat-" MOCK_PLATFORM_32 "/libpreloadP.so",
    "platform/plat-" MOCK_PLATFORM_64 "/libpreloadP.so",
    "in-root-plat-" MOCK_PLATFORM_32 "-only-32-bit.so",
#else
    "opt/" MOCK_LIB_GENERIC "/libpreloadL.so",
    "platform/plat-" MOCK_PLATFORM_GENERIC "/libpreloadP.so",
#endif
  };

  setup (f, context);
  fixture_populate_dir (f, f->mock_host->fd, touch, G_N_ELEMENTS (touch));
  f->env = g_environ_setenv (f->env, "STEAM_COMPAT_CLIENT_INSTALL_PATH",
                             "/steam", TRUE);
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  g_autoptr(GError) local_error = NULL;

  glnx_close_fd (&f->tmpdir_fd);
  glnx_close_fd (&f->mock_runtime_fd);
  glnx_close_fd (&f->var_fd);

  if (f->tmpdir != NULL)
    {
      glnx_shutil_rm_rf_at (-1, f->tmpdir, NULL, &local_error);
      g_assert_no_error (local_error);
    }

  g_clear_object (&f->mock_host);
  g_clear_pointer (&f->mock_runtime, g_free);
  g_clear_pointer (&f->tmpdir, g_free);
  g_clear_pointer (&f->var, g_free);
  g_clear_pointer (&f->env, g_strfreev);
  g_clear_pointer (&f->bwrap, flatpak_bwrap_free);

  tests_check_fd_leaks_leave (f->old_fds);
}

static void
dump_bwrap (FlatpakBwrap *bwrap)
{
  guint i;

  g_test_message ("FlatpakBwrap object:");

  for (i = 0; i < bwrap->argv->len; i++)
    {
      const char *arg = g_ptr_array_index (bwrap->argv, i);

      g_test_message ("\t%s", arg);
    }
}

/* For simplicity we look for argument sequences of length exactly 3:
 * everything we're interested in for this test-case meets that description */
static void
assert_bwrap_contains (FlatpakBwrap *bwrap,
                       const char *one,
                       const char *two,
                       const char *three)
{
  guint i;

  g_assert_cmpuint (bwrap->argv->len, >=, 3);

  for (i = 0; i < bwrap->argv->len - 2; i++)
    {
      if (g_str_equal (g_ptr_array_index (bwrap->argv, i), one)
          && g_str_equal (g_ptr_array_index (bwrap->argv, i + 1), two)
          && g_str_equal (g_ptr_array_index (bwrap->argv, i + 2), three))
        return;
    }

  dump_bwrap (bwrap);
  g_error ("Expected to find: %s %s %s", one, two, three);
}

static void
assert_bwrap_does_not_contain (FlatpakBwrap *bwrap,
                               const char *path)
{
  guint i;

  for (i = 0; i < bwrap->argv->len; i++)
    {
      const char *arg = g_ptr_array_index (bwrap->argv, i);

      g_assert_cmpstr (arg, !=, NULL);
      g_assert_cmpstr (arg, !=, path);
    }
}

static void
test_bind_into_container (Fixture *f,
                          gconstpointer context)
{
  const Config *config = context;
  g_autoptr(PvRuntime) runtime = NULL;
  g_autoptr(GError) error = NULL;
  gboolean ok;

  runtime = fixture_create_runtime (f, config->runtime_flags);

  /* Successful cases */

  ok = pv_runtime_bind_into_container (runtime, f->bwrap,
                                       "/etc/machine-id", NULL, 0,
                                       "/etc/machine-id",
                                       PV_RUNTIME_EMULATION_ROOTS_BOTH,
                                       &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  ok = pv_runtime_bind_into_container (runtime, f->bwrap,
                                       "/etc/arm-file", NULL, 0,
                                       "/etc/arm-file",
                                       PV_RUNTIME_EMULATION_ROOTS_REAL_ONLY,
                                       &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  ok = pv_runtime_bind_into_container (runtime, f->bwrap,
                                       "/fex/etc/x86-file", NULL, 0,
                                       "/etc/x86-file",
                                       PV_RUNTIME_EMULATION_ROOTS_INTERPRETER_ONLY,
                                       &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  /* Error cases */

  ok = pv_runtime_bind_into_container (runtime, f->bwrap,
                                       "/nope", NULL, 0,
                                       "/nope",
                                       PV_RUNTIME_EMULATION_ROOTS_REAL_ONLY,
                                       &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_READ_ONLY);
  g_assert_false (ok);
  g_test_message ("Editing /nope not allowed, as expected: %s", error->message);
  g_clear_error (&error);

  ok = pv_runtime_bind_into_container (runtime, f->bwrap,
                                       "/usr/foo", NULL, 0,
                                       "/usr/foo",
                                       PV_RUNTIME_EMULATION_ROOTS_BOTH,
                                       &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_READ_ONLY);
  g_assert_false (ok);
  g_test_message ("Editing /usr/foo not allowed, as expected: %s", error->message);
  g_clear_error (&error);

  /* Check that the right things happened */

  dump_bwrap (f->bwrap);
  assert_bwrap_does_not_contain (f->bwrap, "/nope");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/foo");
  assert_bwrap_contains (f->bwrap,
                         "--ro-bind", "/etc/machine-id", "/etc/machine-id");
  assert_bwrap_contains (f->bwrap,
                         "--ro-bind", "/etc/arm-file", "/etc/arm-file");
  assert_bwrap_does_not_contain (f->bwrap,
                                 PV_RUNTIME_PATH_INTERPRETER_ROOT "/etc/arm-file");

  if (config->runtime_flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT)
    {
      assert_bwrap_contains (f->bwrap,
                             "--ro-bind", "/etc/machine-id",
                             PV_RUNTIME_PATH_INTERPRETER_ROOT "/etc/machine-id");
      assert_bwrap_contains (f->bwrap,
                             "--ro-bind", "/fex/etc/x86-file",
                             PV_RUNTIME_PATH_INTERPRETER_ROOT "/etc/x86-file");
      assert_bwrap_does_not_contain (f->bwrap, "/etc/x86-file");
    }
  else
    {
      assert_bwrap_contains (f->bwrap,
                             "--ro-bind", "/fex/etc/x86-file", "/etc/x86-file");
      assert_bwrap_does_not_contain (f->bwrap,
                                     PV_RUNTIME_PATH_INTERPRETER_ROOT "/etc/os-machine-id");
      assert_bwrap_does_not_contain (f->bwrap,
                                     PV_RUNTIME_PATH_INTERPRETER_ROOT "/etc/x86-file");
    }
}

static void
test_bind_merged_usr (Fixture *f,
                      gconstpointer context)
{
  static const char * const paths[] =
  {
    "bin>usr/bin",
    "home/",
    "lib>usr/lib",
    "lib32>usr/lib32",
    "lib64>usr/lib",
    "libexec>usr/libexec",
    "opt/",
    "sbin>usr/bin",
    "usr/",
  };
  g_autoptr(GError) local_error = NULL;
  gboolean ret;

  fixture_populate_dir (f, f->mock_host->fd, paths, G_N_ELEMENTS (paths));
  ret = pv_bwrap_bind_usr (f->bwrap, "/provider", f->mock_host->fd, "/run/gfx",
                           &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);
  dump_bwrap (f->bwrap);

  assert_bwrap_contains (f->bwrap, "--symlink", "usr/bin", "/run/gfx/bin");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/lib", "/run/gfx/lib");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/lib", "/run/gfx/lib64");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/lib32", "/run/gfx/lib32");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/bin", "/run/gfx/sbin");
  assert_bwrap_contains (f->bwrap, "--ro-bind", "/provider/usr", "/run/gfx/usr");
  assert_bwrap_does_not_contain (f->bwrap, "home");
  assert_bwrap_does_not_contain (f->bwrap, "/home");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/home");
  assert_bwrap_does_not_contain (f->bwrap, "libexec");
  assert_bwrap_does_not_contain (f->bwrap, "/libexec");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/libexec");
  assert_bwrap_does_not_contain (f->bwrap, "opt");
  assert_bwrap_does_not_contain (f->bwrap, "/opt");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/opt");
}

static void
test_bind_unmerged_usr (Fixture *f,
                        gconstpointer context)
{
  static const char * const paths[] =
  {
    "bin/",
    "home/",
    "lib/",
    "lib64/",
    "libexec/",
    "opt/",
    "sbin/",
    "usr/",
  };
  g_autoptr(GError) local_error = NULL;
  gboolean ret;

  fixture_populate_dir (f, f->mock_host->fd, paths, G_N_ELEMENTS (paths));
  ret = pv_bwrap_bind_usr (f->bwrap, "/provider", f->mock_host->fd, "/run/gfx",
                           &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);
  dump_bwrap (f->bwrap);

  assert_bwrap_contains (f->bwrap, "--ro-bind", "/provider/bin", "/run/gfx/bin");
  assert_bwrap_contains (f->bwrap, "--ro-bind", "/provider/lib", "/run/gfx/lib");
  assert_bwrap_contains (f->bwrap, "--ro-bind", "/provider/lib64", "/run/gfx/lib64");
  assert_bwrap_contains (f->bwrap, "--ro-bind", "/provider/sbin", "/run/gfx/sbin");
  assert_bwrap_contains (f->bwrap, "--ro-bind", "/provider/usr", "/run/gfx/usr");
  assert_bwrap_does_not_contain (f->bwrap, "home");
  assert_bwrap_does_not_contain (f->bwrap, "/home");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/home");
  assert_bwrap_does_not_contain (f->bwrap, "libexec");
  assert_bwrap_does_not_contain (f->bwrap, "/libexec");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/libexec");
  assert_bwrap_does_not_contain (f->bwrap, "opt");
  assert_bwrap_does_not_contain (f->bwrap, "/opt");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/opt");
}

static void
test_bind_usr (Fixture *f,
               gconstpointer context)
{
  static const char * const paths[] =
  {
    "bin/",
    "lib/",
    "lib64/",
    "libexec/",
    "local/",
    "share/",
  };
  g_autoptr(GError) local_error = NULL;
  gboolean ret;

  fixture_populate_dir (f, f->mock_host->fd, paths, G_N_ELEMENTS (paths));
  ret = pv_bwrap_bind_usr (f->bwrap, "/provider", f->mock_host->fd, "/run/gfx",
                           &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);
  dump_bwrap (f->bwrap);

  assert_bwrap_contains (f->bwrap, "--ro-bind", "/provider", "/run/gfx/usr");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/bin", "/run/gfx/bin");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/lib", "/run/gfx/lib");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/lib64", "/run/gfx/lib64");
  assert_bwrap_does_not_contain (f->bwrap, "local");
  assert_bwrap_does_not_contain (f->bwrap, "/local");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/local");
  assert_bwrap_does_not_contain (f->bwrap, "share");
  assert_bwrap_does_not_contain (f->bwrap, "/share");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/share");
}

/*
 * Test that pv_export_root_dirs_like_filesystem_host() behaves the same
 * as Flatpak --filesystem=host.
 */
static void
test_export_root_dirs (Fixture *f,
                       gconstpointer context)
{
  static const char * const paths[] =
  {
    "boot/",
    "bin>usr/bin",
    "dev/pts/",
    "etc/hosts",
    "games/SteamLibrary/",
    "home/user/.steam",
    "lib>usr/lib",
    "lib32>usr/lib32",
    "lib64>usr/lib",
    "libexec>usr/libexec",
    "opt/extras/kde/",
    "proc/1/fd/",
    "root/",
    "run/dbus/",
    "run/gfx/",
    "run/host/",
    "run/media/",
    "run/pressure-vessel/",
    "run/systemd/",
    "tmp/",
    "sbin>usr/bin",
    "sys/",
    "usr/local/",
    "var/tmp/",
  };
  g_autoptr(FlatpakExports) exports = fixture_create_exports (f);
  g_autoptr(GError) local_error = NULL;
  gboolean ret;

  fixture_populate_dir (f, f->mock_host->fd, paths, G_N_ELEMENTS (paths));
  ret = pv_export_root_dirs_like_filesystem_host (f->mock_host->fd, exports,
                                                  FLATPAK_FILESYSTEM_MODE_READ_WRITE,
                                                  _srt_dirent_strcmp,
                                                  &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);
  flatpak_exports_append_bwrap_args (exports, f->bwrap);

  dump_bwrap (f->bwrap);

  /* We don't export mutable OS state in this particular function,
   * for parity with Flatpak --filesystem=host (which does not imply
   * --filesystem=/tmp or --filesystem=/var) */
  assert_bwrap_does_not_contain (f->bwrap, "/etc");
  assert_bwrap_does_not_contain (f->bwrap, "/tmp");
  assert_bwrap_does_not_contain (f->bwrap, "/var");

  /* We do export miscellaneous top-level directories */
  assert_bwrap_contains (f->bwrap, "--bind", "/games", "/games");
  assert_bwrap_contains (f->bwrap, "--bind", "/home", "/home");
  assert_bwrap_contains (f->bwrap, "--bind", "/opt", "/opt");

  /* /run/media gets a special case here for parity with Flatpak's
   * --filesystem=host, even though it's not top-level */
  assert_bwrap_contains (f->bwrap, "--bind", "/run/media", "/run/media");

  /* We don't export /usr and friends in this particular function
   * (flatpak --filesystem=host would mount them in /run/host instead) */
  assert_bwrap_does_not_contain (f->bwrap, "/bin");
  assert_bwrap_does_not_contain (f->bwrap, "/lib");
  assert_bwrap_does_not_contain (f->bwrap, "/lib32");
  assert_bwrap_does_not_contain (f->bwrap, "/lib64");
  assert_bwrap_does_not_contain (f->bwrap, "/usr");
  assert_bwrap_does_not_contain (f->bwrap, "/sbin");

  /* We don't export these for various reasons */
  assert_bwrap_does_not_contain (f->bwrap, "/app");
  assert_bwrap_does_not_contain (f->bwrap, "/boot");
  assert_bwrap_does_not_contain (f->bwrap, "/dev");
  assert_bwrap_does_not_contain (f->bwrap, "/dev/pts");
  assert_bwrap_does_not_contain (f->bwrap, "/libexec");
  assert_bwrap_does_not_contain (f->bwrap, "/proc");
  assert_bwrap_does_not_contain (f->bwrap, "/root");
  assert_bwrap_does_not_contain (f->bwrap, "/run");
  assert_bwrap_does_not_contain (f->bwrap, "/run/dbus");
  assert_bwrap_does_not_contain (f->bwrap, "/run/gfx");
  assert_bwrap_does_not_contain (f->bwrap, "/run/host");
  assert_bwrap_does_not_contain (f->bwrap, "/run/pressure-vessel");
  assert_bwrap_does_not_contain (f->bwrap, "/run/systemd");
  assert_bwrap_does_not_contain (f->bwrap, "/sys");

  /* We would export these if they existed, but they don't */
  assert_bwrap_does_not_contain (f->bwrap, "/mnt");
  assert_bwrap_does_not_contain (f->bwrap, "/srv");
}

static void
test_make_symlink_in_container (Fixture *f,
                                gconstpointer context)
{
  const Config *config = context;
  g_autoptr(GError) error = NULL;
  g_autoptr(PvRuntime) runtime = NULL;
  gboolean ok;
  SrtSysroot *mutable_sysroot;

  runtime = fixture_create_runtime (f, config->runtime_flags);
  mutable_sysroot = pv_runtime_get_mutable_sysroot (runtime);

  if (config->runtime_flags & PV_RUNTIME_FLAGS_COPY_RUNTIME)
    g_assert_nonnull (mutable_sysroot);
  else
    g_assert_null (mutable_sysroot);

  /* Successful cases */

  ok = pv_runtime_make_symlink_in_container (runtime, f->bwrap,
                                             "../usr/lib/os-release",
                                             "/etc/os-release",
                                             PV_RUNTIME_EMULATION_ROOTS_BOTH,
                                             &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  ok = pv_runtime_make_symlink_in_container (runtime, f->bwrap,
                                             "/run/host/foo",
                                             "/var/foo",
                                             PV_RUNTIME_EMULATION_ROOTS_REAL_ONLY,
                                             &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  ok = pv_runtime_make_symlink_in_container (runtime, f->bwrap,
                                             "/run/x86/bar",
                                             "/var/bar",
                                             PV_RUNTIME_EMULATION_ROOTS_INTERPRETER_ONLY,
                                             &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  /* Conditionally OK, if there is an on-disk directory we can edit */

  ok = pv_runtime_make_symlink_in_container (runtime, f->bwrap,
                                             "/run/host/foo",
                                             "/usr/foo",
                                             PV_RUNTIME_EMULATION_ROOTS_REAL_ONLY,
                                             &error);

  if (mutable_sysroot == NULL)
    {
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_READ_ONLY);
      g_assert_false (ok);
      g_test_message ("Editing /usr not allowed, as expected: %s",
                      error->message);
      g_clear_error (&error);
    }
  else if (config->runtime_flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT)
    {
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_READ_ONLY);
      g_assert_false (ok);
      g_test_message ("Editing real /usr not allowed, as expected: %s",
                      error->message);
      g_clear_error (&error);
    }
  else
    {
      g_assert_no_error (error);
      g_assert_true (ok);

    }

  ok = pv_runtime_make_symlink_in_container (runtime, f->bwrap,
                                             "/run/x86/bar",
                                             "/usr/bar",
                                             PV_RUNTIME_EMULATION_ROOTS_INTERPRETER_ONLY,
                                             &error);

  if (mutable_sysroot == NULL)
    {
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_READ_ONLY);
      g_assert_false (ok);
      g_test_message ("Editing /usr not allowed, as expected: %s",
                      error->message);
      g_clear_error (&error);
    }
  else
    {
      g_assert_no_error (error);
      g_assert_true (ok);
    }

  ok = pv_runtime_make_symlink_in_container (runtime, f->bwrap,
                                             "/run/baz",
                                             "/usr/baz",
                                             PV_RUNTIME_EMULATION_ROOTS_BOTH,
                                             &error);

  if (mutable_sysroot == NULL)
    {
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_READ_ONLY);
      g_assert_false (ok);
      g_test_message ("Editing /usr not allowed, as expected: %s",
                      error->message);
      g_clear_error (&error);
    }
  else if (config->runtime_flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT)
    {
      g_assert_error (error, G_IO_ERROR, G_IO_ERROR_READ_ONLY);
      g_assert_false (ok);
      g_test_message ("Editing real /usr not allowed, as expected: %s",
                      error->message);
      g_clear_error (&error);
    }
  else
    {
      g_assert_no_error (error);
      g_assert_true (ok);
    }

  /* Error cases */

  ok = pv_runtime_make_symlink_in_container (runtime, f->bwrap,
                                             "/nope",
                                             "/nope",
                                             PV_RUNTIME_EMULATION_ROOTS_REAL_ONLY,
                                             &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_READ_ONLY);
  g_assert_false (ok);
  g_test_message ("Editing /nope not allowed, as expected: %s", error->message);
  g_clear_error (&error);

  /* Check that the right things happened */

  dump_bwrap (f->bwrap);
  assert_bwrap_does_not_contain (f->bwrap, "/nope");
  /* /etc/os-release is in the real root (and, if used, the interpreter
   * root, but that's checked later) */
  assert_bwrap_contains (f->bwrap,
                         "--symlink", "../usr/lib/os-release", "/etc/os-release");
  /* /var/foo is in the real root only */
  assert_bwrap_contains (f->bwrap,
                         "--symlink", "/run/host/foo", "/var/foo");
  assert_bwrap_does_not_contain (f->bwrap,
                                 PV_RUNTIME_PATH_INTERPRETER_ROOT "/var/foo");

  if (config->runtime_flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT)
    {
      /* /etc/os-release is in the interpreter root (and the real root) */
      assert_bwrap_contains (f->bwrap,
                             "--symlink", "../usr/lib/os-release",
                             PV_RUNTIME_PATH_INTERPRETER_ROOT "/etc/os-release");
      /* /var/bar is in the interpreter root only */
      assert_bwrap_contains (f->bwrap,
                             "--symlink", "/run/x86/bar",
                             PV_RUNTIME_PATH_INTERPRETER_ROOT "/var/bar");
    }
  else
    {
      /* We're not using an interpreter root */
      assert_bwrap_does_not_contain (f->bwrap,
                                     PV_RUNTIME_PATH_INTERPRETER_ROOT "/etc/os-release");
      assert_bwrap_does_not_contain (f->bwrap,
                                     PV_RUNTIME_PATH_INTERPRETER_ROOT "/var/bar");

      /* /var/bar would have been in the interpreter root only, but because
       * we don't have an interpreter root, it ends up in the real root */
      assert_bwrap_contains (f->bwrap, "--symlink", "/run/x86/bar", "/var/bar");
    }

  /* We must not try to edit /usr with --symlink: that can't work,
   * because /usr is read-only */
  assert_bwrap_does_not_contain (f->bwrap, "/usr/foo");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/bar");
  assert_bwrap_does_not_contain (f->bwrap, "/usr/baz");
  assert_bwrap_does_not_contain (f->bwrap,
                                 PV_RUNTIME_PATH_INTERPRETER_ROOT "/usr/foo");
  assert_bwrap_does_not_contain (f->bwrap,
                                 PV_RUNTIME_PATH_INTERPRETER_ROOT "/usr/bar");
  assert_bwrap_does_not_contain (f->bwrap,
                                 PV_RUNTIME_PATH_INTERPRETER_ROOT "/usr/baz");

  if (mutable_sysroot != NULL)
    {
      g_autofree gchar *target = NULL;
      struct stat stat_buf;

      /* /usr/foo is only created if the mutable sysroot is the real root */
      target = glnx_readlinkat_malloc (mutable_sysroot->fd,
                                       "usr/foo", NULL, NULL);

      if (config->runtime_flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT)
        g_assert_cmpstr (target, ==, NULL);
      else
        g_assert_cmpstr (target, ==, "/run/host/foo");

      g_clear_pointer (&target, g_free);

      /* /usr/bar is created if the mutable sysroot is the interpreter root,
       * or if we are not using a separate interpreter root */
      target = glnx_readlinkat_malloc (mutable_sysroot->fd,
                                       "usr/bar", NULL, NULL);
      g_assert_cmpstr (target, ==, "/run/x86/bar");
      g_clear_pointer (&target, g_free);

      /* /usr/baz was only created if we are not using a separate
       * interpreter root, because if we were, we would have been unable
       * to create it in both roots */
      target = glnx_readlinkat_malloc (mutable_sysroot->fd,
                                       "usr/baz", NULL, NULL);

      if (config->runtime_flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT)
        g_assert_cmpstr (target, ==, NULL);
      else
        g_assert_cmpstr (target, ==, "/run/baz");

      g_clear_pointer (&target, g_free);

      /* We never create/edit the interpreter root as a subdir of the
       * mutable sysroot */
      g_assert_cmpint (fstatat (mutable_sysroot->fd,
                                "run/pressure-vessel/interpreter-root",
                                &stat_buf, 0) == 0 ? 0 : errno,
                       ==, ENOENT);
    }
}

static void
populate_ld_preload (Fixture *f,
                     GPtrArray *argv,
                     PvAppendPreloadFlags flags,
                     PvRuntime *runtime,
                     FlatpakExports *exports)
{
  static const struct
  {
    const char *string;
    const char *warning;
  } preloads[] =
  {
    { "", .warning = "Ignoring invalid loadable module \"\"" },
    { "", .warning = "Ignoring invalid loadable module \"\"" },
    { "/app/lib/libpreloadA.so" },
    { "/platform/plat-$PLATFORM/libpreloadP.so" },
    { "/opt/${LIB}/libpreloadL.so" },
    { "/lib/libpreload-rootfs.so" },
    { "/usr/lib/libpreloadU.so" },
    { "/home/me/libpreloadH.so" },
    { "/steam/lib/gameoverlayrenderer.so" },
    { "/overlay/libs/${ORIGIN}/../lib/libpreloadO.so" },
    { "/future/libs-$FUTURE/libpreloadF.so" },
    { "/in-root-plat-${PLATFORM}-only-32-bit.so" },
    { "/in-root-${FUTURE}.so" },
    { "./${RELATIVE}.so" },
    { "./relative.so" },
    { "libfakeroot.so" },
    { "libpthread.so.0" },
    {
      "/usr/local/lib/libgtk3-nocsd.so.0",
      .warning = "Disabling gtk3-nocsd LD_PRELOAD: it is known to cause crashes.",
    },
    { "", .warning = "Ignoring invalid loadable module \"\"" },
  };
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (preloads); i++)
    {
      GLogLevelFlags old_fatal_mask = G_LOG_FATAL_MASK;

      /* We expect a warning for libgtk3-nocsd.so.0, but the test framework
       * makes warnings and critical warnings fatal, in addition to the
       * usual fatal errors. Temporarily relax that to just critical
       * warnings and fatal errors. */
      if (preloads[i].warning != NULL)
        {
          old_fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
#if GLIB_CHECK_VERSION(2, 34, 0)
          /* We can't check for the message during unit testing when
           * compiling with GLib 2.32 from scout, but we can check for it
           * in developer builds against a newer GLib. Note that this
           * assumes pressure-vessel doesn't define G_LOG_USE_STRUCTURED,
           * but that's a GLib 2.50 feature which we are unlikely to use
           * while we are still building against scout. */
          g_test_expect_message ("pressure-vessel",
                                 G_LOG_LEVEL_WARNING,
                                 preloads[i].warning);
#endif
        }

      pv_wrap_append_preload (argv,
                              "LD_PRELOAD",
                              "--ld-preload",
                              preloads[i].string,
                              f->env,
                              flags | PV_APPEND_PRELOAD_FLAGS_IN_UNIT_TESTS,
                              runtime,
                              exports);

      /* If we modified the fatal mask, put back the old value. */
      if (preloads[i].warning != NULL)
        {
#if GLIB_CHECK_VERSION(2, 34, 0)
          g_test_assert_expected_messages ();
#endif
          g_log_set_always_fatal (old_fatal_mask);
        }
    }

  for (i = 0; i < argv->len; i++)
    g_test_message ("argv[%" G_GSIZE_FORMAT "]: %s",
                    i, (const char *) g_ptr_array_index (argv, i));

  g_test_message ("argv->len: %" G_GSIZE_FORMAT, i);
}

static const char * const expected_preload_paths[] =
{
  "/app/lib/libpreloadA.so",
#if defined(__i386__) || defined(__x86_64__)
  "/platform/plat-" MOCK_PLATFORM_64 "/libpreloadP.so:abi=" SRT_ABI_X86_64,
  "/platform/plat-" MOCK_PLATFORM_32 "/libpreloadP.so:abi=" SRT_ABI_I386,
  "/opt/" MOCK_LIB_64 "/libpreloadL.so:abi=" SRT_ABI_X86_64,
  "/opt/" MOCK_LIB_32 "/libpreloadL.so:abi=" SRT_ABI_I386,
#else
  "/platform/plat-" MOCK_PLATFORM_GENERIC "/libpreloadP.so:abi=" MOCK_ABI,
  "/opt/" MOCK_LIB_GENERIC "/libpreloadL.so:abi=" MOCK_ABI,
#endif
  "/lib/libpreload-rootfs.so",
  "/usr/lib/libpreloadU.so",
  "/home/me/libpreloadH.so",
  "/steam/lib/gameoverlayrenderer.so",
  "/overlay/libs/${ORIGIN}/../lib/libpreloadO.so",
  "/future/libs-$FUTURE/libpreloadF.so",
#if defined(__i386__) || defined(__x86_64__)
  "/in-root-plat-i686-only-32-bit.so:abi=" SRT_ABI_I386,
#endif
  "/in-root-${FUTURE}.so",
  "./${RELATIVE}.so",
  "./relative.so",
  /* Our mock implementation of pv_runtime_has_library() behaves as though
   * libfakeroot is not in the runtime or graphics stack provider, only
   * the current namespace */
#if defined(__i386__) || defined(__x86_64__)
  "/path/to/" MOCK_LIB_64 "/libfakeroot.so:abi=" SRT_ABI_X86_64,
  "/path/to/" MOCK_LIB_32 "/libfakeroot.so:abi=" SRT_ABI_I386,
#else
  "/path/to/" MOCK_LIB_GENERIC "/libfakeroot.so:abi=" MOCK_ABI,
#endif
  /* Our mock implementation of pv_runtime_has_library() behaves as though
   * libpthread.so.0 *is* in the runtime, as we would expect */
  "libpthread.so.0",
};

static void
test_remap_ld_preload (Fixture *f,
                       gconstpointer context)
{
  g_autoptr(FlatpakExports) exports = fixture_create_exports (f);
  g_autoptr(GPtrArray) argv = g_ptr_array_new_with_free_func (g_free);
  g_autoptr(PvRuntime) runtime = fixture_create_runtime (f, PV_RUNTIME_FLAGS_NONE);
  gsize i;

  populate_ld_preload (f, argv, PV_APPEND_PRELOAD_FLAGS_NONE, runtime, exports);

  g_assert_cmpuint (argv->len, ==, G_N_ELEMENTS (expected_preload_paths));

  for (i = 0; i < argv->len; i++)
    {
      char *argument = g_ptr_array_index (argv, i);
      g_assert_true (g_str_has_prefix (argument, "--ld-preload="));
      argument += strlen("--ld-preload=");

      if (g_str_has_prefix (expected_preload_paths[i], "/lib/")
          || g_str_has_prefix (expected_preload_paths[i], "/usr/lib/"))
        {
          g_assert_true (g_str_has_prefix (argument, "/run/host/"));
          argument += strlen("/run/host");
        }

      g_assert_cmpstr (argument, ==, expected_preload_paths[i]);
    }

  /* FlatpakExports never exports /app */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/app"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/app/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/app/lib/libpreloadA.so"));

  /* We don't always export /home etc. so we have to explicitly export
   * this one */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/home"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/home/me"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/home/me/libpreloadH.so"));

  /* We don't always export /opt and /platform, so we have to explicitly export
   * these. */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/opt"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/opt/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/platform"));
#if defined(__i386__) || defined(__x86_64__)
  g_assert_true (flatpak_exports_path_is_visible (exports, "/opt/" MOCK_LIB_32 "/libpreloadL.so"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/opt/" MOCK_LIB_64 "/libpreloadL.so"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/platform/plat-" MOCK_PLATFORM_32 "/libpreloadP.so"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/platform/plat-" MOCK_PLATFORM_64 "/libpreloadP.so"));
#else
  g_assert_true (flatpak_exports_path_is_visible (exports, "/opt/" MOCK_LIB_GENERIC "/libpreloadL.so"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/platform/plat-" MOCK_PLATFORM_GENERIC "/libpreloadP.so"));
#endif

  /* FlatpakExports never exports /lib as /lib */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/lib/libpreload-rootfs.so"));

  /* FlatpakExports never exports /usr as /usr */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/usr"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/usr/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/usr/lib/libpreloadU.so"));

  /* We assume STEAM_COMPAT_CLIENT_INSTALL_PATH is dealt with separately */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/steam"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/steam/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/steam/lib/gameoverlayrenderer.so"));

  /* We don't know what ${ORIGIN} will expand to, so we have to cut off at
   * /overlay/libs */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/overlay"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/overlay/libs"));

  /* We don't know what ${FUTURE} will expand to, so we have to cut off at
   * /future */
  g_assert_true (flatpak_exports_path_is_visible (exports, "/future"));

  /* We don't export the entire root directory just because it has a
   * module in it */
  g_assert_true (flatpak_exports_path_is_visible (exports, "/"));
}

static void
test_remap_ld_preload_flatpak (Fixture *f,
                               gconstpointer context)
{
  g_autoptr(GPtrArray) argv = g_ptr_array_new_with_free_func (g_free);
  g_autoptr(PvRuntime) runtime = fixture_create_runtime (f, PV_RUNTIME_FLAGS_FLATPAK_SUBSANDBOX);
  gsize i;

  populate_ld_preload (f, argv, PV_APPEND_PRELOAD_FLAGS_FLATPAK_SUBSANDBOX,
                       runtime, NULL);

  g_assert_cmpuint (argv->len, ==, G_N_ELEMENTS (expected_preload_paths));

  for (i = 0; i < argv->len; i++)
    {
      char *argument = g_ptr_array_index (argv, i);
      g_assert_true (g_str_has_prefix (argument, "--ld-preload="));
      argument += strlen("--ld-preload=");

      if (g_str_has_prefix (expected_preload_paths[i], "/app/")
          || g_str_has_prefix (expected_preload_paths[i], "/lib/")
          || g_str_has_prefix (expected_preload_paths[i], "/usr/lib/"))
        {
          g_assert_true (g_str_has_prefix (argument, "/run/parent/"));
          argument += strlen("/run/parent");
        }

      g_assert_cmpstr (argument, ==, expected_preload_paths[i]);
    }
}

/*
 * In addition to testing the rare case where there's no runtime,
 * this one also exercises PV_APPEND_PRELOAD_FLAGS_REMOVE_GAME_OVERLAY,
 * which is the implementation of --remove-game-overlay.
 */
static void
test_remap_ld_preload_no_runtime (Fixture *f,
                                  gconstpointer context)
{
  g_autoptr(GPtrArray) argv = g_ptr_array_new_with_free_func (g_free);
  g_autoptr(FlatpakExports) exports = fixture_create_exports (f);
  gsize i, j;

  populate_ld_preload (f, argv, PV_APPEND_PRELOAD_FLAGS_REMOVE_GAME_OVERLAY,
                       NULL, exports);

  g_assert_cmpuint (argv->len, ==, G_N_ELEMENTS (expected_preload_paths) - 1);

  for (i = 0, j = 0; i < argv->len; i++, j++)
    {
      char *argument = g_ptr_array_index (argv, i);
      g_assert_true (g_str_has_prefix (argument, "--ld-preload="));
      argument += strlen("--ld-preload=");

      /* /steam/lib/gameoverlayrenderer.so is missing because we used the
       * REMOVE_GAME_OVERLAY flag */
      if (g_str_has_suffix (expected_preload_paths[j], "/gameoverlayrenderer.so"))
        {
          /* We expect to skip only one element */
          g_assert_cmpint (i, ==, j);
          j++;
        }

      g_assert_cmpstr (argument, ==, expected_preload_paths[j]);
    }

  /* FlatpakExports never exports /app */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/app"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/app/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/app/lib/libpreloadA.so"));

  /* We don't always export /home etc. so we have to explicitly export
   * this one */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/home"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/home/me"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/home/me/libpreloadH.so"));

  /* We don't always export /opt and /platform, so we have to explicitly export
   * these. */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/opt"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/opt/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/platform"));
#if defined(__i386__) || defined(__x86_64__)
  g_assert_true (flatpak_exports_path_is_visible (exports, "/opt/" MOCK_LIB_32 "/libpreloadL.so"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/opt/" MOCK_LIB_64 "/libpreloadL.so"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/platform/plat-" MOCK_PLATFORM_32 "/libpreloadP.so"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/platform/plat-" MOCK_PLATFORM_64 "/libpreloadP.so"));
#else
  g_assert_true (flatpak_exports_path_is_visible (exports, "/opt/" MOCK_LIB_GENERIC "/libpreloadL.so"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/platform/plat-" MOCK_PLATFORM_GENERIC "/libpreloadP.so"));
#endif

  /* FlatpakExports never exports /lib as /lib */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/lib/libpreload-rootfs.so"));

  /* FlatpakExports never exports /usr as /usr */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/usr"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/usr/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/usr/lib/libpreloadU.so"));

  /* We don't know what ${ORIGIN} will expand to, so we have to cut off at
   * /overlay/libs */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/overlay"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/overlay/libs"));

  /* We don't know what ${FUTURE} will expand to, so we have to cut off at
   * /future */
  g_assert_true (flatpak_exports_path_is_visible (exports, "/future"));

  /* We don't export the entire root directory just because it has a
   * module in it */
  g_assert_true (flatpak_exports_path_is_visible (exports, "/"));
}

static void
test_remap_ld_preload_flatpak_no_runtime (Fixture *f,
                                          gconstpointer context)
{
  g_autoptr(GPtrArray) argv = g_ptr_array_new_with_free_func (g_free);
  gsize i;

  populate_ld_preload (f, argv, PV_APPEND_PRELOAD_FLAGS_FLATPAK_SUBSANDBOX,
                       NULL, NULL);

  g_assert_cmpuint (argv->len, ==, G_N_ELEMENTS (expected_preload_paths));

  for (i = 0; i < argv->len; i++)
    {
      char *argument = g_ptr_array_index (argv, i);
      g_assert_true (g_str_has_prefix (argument, "--ld-preload="));
      argument += strlen("--ld-preload=");

      g_assert_cmpstr (argument, ==, expected_preload_paths[i]);
    }
}

/*
 * Test that pv_wrap_use_home(PV_HOME_MODE_SHARED) makes nearly everything
 * available.
 */
static void
test_use_home_shared (Fixture *f,
                      gconstpointer context)
{
  static const char * const paths[] =
  {
    "app/",
    "bin>usr/bin",
    "config/",
    "dangling>nonexistent",
    "data/",
    "dev/pts/",
    "etc/hosts",
    "games/SteamLibrary/",
    "home/user/.config/",
    "home/user/.config/cef_user_data>../../config/cef_user_data",
    "home/user/.local/",
    "home/user/.local/share>../../../data",
    "home/user/.steam",
    "lib>usr/lib",
    "lib32>usr/lib32",
    "lib64>usr/lib",
    "libexec>usr/libexec",
    "media/",
    "mnt/",
    "offload/user/data/",
    "offload/user/state/",
    "offload/rw2/",
    "overrides/forbidden/",
    "proc/1/fd/",
    "ro/",
    "root/",
    "run/dbus/",
    "run/gfx/",
    "run/host/",
    "run/pressure-vessel/",
    "run/systemd/",
    "rw/",
    "rw2>offload/rw2",
    "sbin>usr/bin",
    "single:/dir:/and:/deprecated/",
    "srv/data/",
    "sys/",
    "tmp/",
    "usr/local/",
    "var/tmp/",
  };
  static const char * const mock_environ[] =
  {
    "STEAM_COMPAT_TOOL_PATH=/single:/dir:/and:/deprecated",
    "STEAM_COMPAT_MOUNTS=/overrides/forbidden",
    "PRESSURE_VESSEL_FILESYSTEMS_RO=/ro",
    "PRESSURE_VESSEL_FILESYSTEMS_RW=:/rw:/rw2:/nonexistent:::::",
    NULL
  };
  g_autoptr(FlatpakBwrap) env_bwrap = NULL;
  g_autoptr(FlatpakExports) exports = fixture_create_exports (f);
  g_autoptr(FlatpakExports) env_exports = fixture_create_exports (f);
  g_autoptr(GError) local_error = NULL;
  g_autoptr(PvEnviron) container_env = pv_environ_new ();
  GLogLevelFlags was_fatal;
  gboolean ret;

  fixture_populate_dir (f, f->mock_host->fd, paths, G_N_ELEMENTS (paths));
  ret = pv_wrap_use_home (PV_HOME_MODE_SHARED, "/home/user", NULL, exports,
                          f->bwrap, container_env, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);
  flatpak_exports_append_bwrap_args (exports, f->bwrap);

  dump_bwrap (f->bwrap);

  /* /usr and friends are out of scope here */
  assert_bwrap_does_not_contain (f->bwrap, "/bin");
  assert_bwrap_does_not_contain (f->bwrap, "/lib");
  assert_bwrap_does_not_contain (f->bwrap, "/lib32");
  assert_bwrap_does_not_contain (f->bwrap, "/lib64");
  assert_bwrap_does_not_contain (f->bwrap, "/usr");
  assert_bwrap_does_not_contain (f->bwrap, "/sbin");

  /* Various FHS and FHS-adjacent directories go along with the home
   * directory */
  assert_bwrap_contains (f->bwrap, "--bind", "/home", "/home");
  assert_bwrap_contains (f->bwrap, "--bind", "/media", "/media");
  assert_bwrap_contains (f->bwrap, "--bind", "/mnt", "/mnt");
  assert_bwrap_contains (f->bwrap, "--bind", "/srv", "/srv");
  assert_bwrap_contains (f->bwrap, "--bind", "/var/tmp", "/var/tmp");

  /* Some directories that are commonly symlinks get handled, by
   * mounting the target of a symlink if any */
  assert_bwrap_contains (f->bwrap, "--bind", "/data", "/data");

  /* Mutable OS state is not tied to the home directory */
  assert_bwrap_does_not_contain (f->bwrap, "/etc");
  assert_bwrap_does_not_contain (f->bwrap, "/var");

  /* We do share /tmp, but this particular function is not responsible
   * for it */
  assert_bwrap_does_not_contain (f->bwrap, "/tmp");

  /* We don't currently export miscellaneous top-level directories */
  assert_bwrap_does_not_contain (f->bwrap, "/games");

  /* /run is out of scope */
  assert_bwrap_does_not_contain (f->bwrap, "/run/dbus");

  /* We don't export these here for various reasons */
  assert_bwrap_does_not_contain (f->bwrap, "/app");
  assert_bwrap_does_not_contain (f->bwrap, "/boot");
  assert_bwrap_does_not_contain (f->bwrap, "/dev");
  assert_bwrap_does_not_contain (f->bwrap, "/dev/pts");
  assert_bwrap_does_not_contain (f->bwrap, "/libexec");
  assert_bwrap_does_not_contain (f->bwrap, "/proc");
  assert_bwrap_does_not_contain (f->bwrap, "/root");
  assert_bwrap_does_not_contain (f->bwrap, "/run");
  assert_bwrap_does_not_contain (f->bwrap, "/run/gfx");
  assert_bwrap_does_not_contain (f->bwrap, "/run/host");
  assert_bwrap_does_not_contain (f->bwrap, "/run/pressure-vessel");
  assert_bwrap_does_not_contain (f->bwrap, "/sys");

  /* We would export these if they existed, but they don't */
  assert_bwrap_does_not_contain (f->bwrap, "/opt");
  assert_bwrap_does_not_contain (f->bwrap, "/run/media");

  env_bwrap = flatpak_bwrap_new (flatpak_bwrap_empty_env);

  /* Don't crash on warnings here */
  was_fatal = g_log_set_always_fatal (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
  pv_bind_and_propagate_from_environ (f->mock_host,
                                      mock_environ,
                                      PV_HOME_MODE_SHARED,
                                      env_exports,
                                      container_env);
  g_log_set_always_fatal (was_fatal);

  flatpak_exports_append_bwrap_args (env_exports, env_bwrap);
  dump_bwrap (env_bwrap);
  assert_bwrap_contains (env_bwrap, "--ro-bind", "/ro", "/ro");
  assert_bwrap_contains (env_bwrap, "--bind", "/rw", "/rw");
  assert_bwrap_contains (env_bwrap, "--symlink", "offload/rw2", "/rw2");
  assert_bwrap_contains (env_bwrap, "--bind", "/offload/rw2", "/offload/rw2");
  /* These are in PRESSURE_VESSEL_FILESYSTEMS_RW but don't actually exist. */
  assert_bwrap_does_not_contain (env_bwrap, "/nonexistent");
  assert_bwrap_does_not_contain (env_bwrap, "/dangling");
  /* STEAM_COMPAT_TOOL_PATH is deprecated (not explicitly tested, but
   * you'll see a warning in the test log), and because it doesn't have
   * the COLON_DELIMITED flag, it's parsed as a single oddly-named
   * directory. */
  assert_bwrap_contains (env_bwrap, "--bind",
                         "/single:/dir:/and:/deprecated",
                         "/single:/dir:/and:/deprecated");
  /* Paths below /overrides are not used, with a warning. */
  assert_bwrap_does_not_contain (env_bwrap, "/overrides/forbidden");
}

/*
 * Test that pv_wrap_use_host_os() makes nearly everything from the host OS
 * available. (This is what we do if run with no runtime, although
 * SteamLinuxRuntime_* never actually does this.)
 */
static void
test_use_host_os (Fixture *f,
                  gconstpointer context)
{
  static const char * const paths[] =
  {
    "boot/",
    "bin>usr/bin",
    "dev/pts/",
    "etc/hosts",
    "games/SteamLibrary/",
    "home/user/.steam",
    "lib>usr/lib",
    "lib32>usr/lib32",
    "lib64>usr/lib",
    "libexec>usr/libexec",
    "opt/extras/kde/",
    "overrides/",
    "proc/1/fd/",
    "root/",
    "run/dbus/",
    "run/gfx/",
    "run/host/",
    "run/media/",
    "run/pressure-vessel/",
    "run/systemd/",
    "tmp/",
    "sbin>usr/bin",
    "sys/",
    "usr/local/",
    "var/tmp/",
  };
  g_autoptr(FlatpakExports) exports = fixture_create_exports (f);
  g_autoptr(GError) local_error = NULL;
  gboolean ret;

  fixture_populate_dir (f, f->mock_host->fd, paths, G_N_ELEMENTS (paths));
  ret = pv_wrap_use_host_os (f->mock_host->fd, exports, f->bwrap,
                             _srt_dirent_strcmp, &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);
  flatpak_exports_append_bwrap_args (exports, f->bwrap);

  dump_bwrap (f->bwrap);

  /* We do export /usr and friends */
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/bin", "/bin");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/lib", "/lib");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/lib", "/lib64");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/lib32", "/lib32");
  assert_bwrap_contains (f->bwrap, "--ro-bind", "/usr", "/usr");
  assert_bwrap_contains (f->bwrap, "--symlink", "usr/bin", "/sbin");

  /* We do export mutable OS state */
  assert_bwrap_contains (f->bwrap, "--bind", "/etc", "/etc");
  assert_bwrap_contains (f->bwrap, "--bind", "/tmp", "/tmp");
  assert_bwrap_contains (f->bwrap, "--bind", "/var", "/var");

  /* We do export miscellaneous top-level directories */
  assert_bwrap_contains (f->bwrap, "--bind", "/games", "/games");
  assert_bwrap_contains (f->bwrap, "--bind", "/home", "/home");
  assert_bwrap_contains (f->bwrap, "--bind", "/opt", "/opt");

  /* We do export most of the contents of /run, but not /run itself */
  assert_bwrap_contains (f->bwrap, "--bind", "/run/dbus", "/run/dbus");
  assert_bwrap_contains (f->bwrap, "--bind", "/run/media", "/run/media");
  assert_bwrap_contains (f->bwrap, "--bind", "/run/systemd", "/run/systemd");

  /* We don't export these in pv_wrap_use_host_os() for various reasons */
  assert_bwrap_does_not_contain (f->bwrap, "/app");
  assert_bwrap_does_not_contain (f->bwrap, "/boot");
  assert_bwrap_does_not_contain (f->bwrap, "/dev");
  assert_bwrap_does_not_contain (f->bwrap, "/dev/pts");
  assert_bwrap_does_not_contain (f->bwrap, "/libexec");
  assert_bwrap_does_not_contain (f->bwrap, "/overrides");
  assert_bwrap_does_not_contain (f->bwrap, "/proc");
  assert_bwrap_does_not_contain (f->bwrap, "/root");
  assert_bwrap_does_not_contain (f->bwrap, "/run");
  assert_bwrap_does_not_contain (f->bwrap, "/run/gfx");
  assert_bwrap_does_not_contain (f->bwrap, "/run/host");
  assert_bwrap_does_not_contain (f->bwrap, "/run/pressure-vessel");
  assert_bwrap_does_not_contain (f->bwrap, "/sys");

  /* We would export these if they existed, but they don't */
  assert_bwrap_does_not_contain (f->bwrap, "/mnt");
  assert_bwrap_does_not_contain (f->bwrap, "/srv");
}

int
main (int argc,
      char **argv)
{
  _srt_setenv_disable_gio_modules ();

  _srt_tests_init (&argc, &argv, NULL);

  g_test_add ("/bind-into-container/normal", Fixture,
              &default_config,
              setup, test_bind_into_container, teardown);
  g_test_add ("/bind-into-container/copy", Fixture,
              &copy_config,
              setup, test_bind_into_container, teardown);
  g_test_add ("/bind-into-container/interpreter-root", Fixture,
              &interpreter_root_config,
              setup, test_bind_into_container, teardown);
  g_test_add ("/bind-merged-usr", Fixture, NULL,
              setup, test_bind_merged_usr, teardown);
  g_test_add ("/bind-unmerged-usr", Fixture, NULL,
              setup, test_bind_unmerged_usr, teardown);
  g_test_add ("/bind-usr", Fixture, NULL,
              setup, test_bind_usr, teardown);
  g_test_add ("/export-root-dirs", Fixture, NULL,
              setup, test_export_root_dirs, teardown);
  g_test_add ("/make-symlink-in-container/normal", Fixture,
              &default_config,
              setup, test_make_symlink_in_container, teardown);
  g_test_add ("/make-symlink-in-container/copy", Fixture,
              &copy_config,
              setup, test_make_symlink_in_container, teardown);
  g_test_add ("/make-symlink-in-container/interpreter-root", Fixture,
              &interpreter_root_config,
              setup, test_make_symlink_in_container, teardown);
  g_test_add ("/remap-ld-preload", Fixture, NULL,
              setup_ld_preload, test_remap_ld_preload, teardown);
  g_test_add ("/remap-ld-preload-flatpak", Fixture, NULL,
              setup_ld_preload, test_remap_ld_preload_flatpak, teardown);
  g_test_add ("/remap-ld-preload-no-runtime", Fixture, NULL,
              setup_ld_preload, test_remap_ld_preload_no_runtime, teardown);
  g_test_add ("/remap-ld-preload-flatpak-no-runtime", Fixture, NULL,
              setup_ld_preload, test_remap_ld_preload_flatpak_no_runtime, teardown);
  g_test_add ("/use-home/shared", Fixture, NULL,
              setup, test_use_home_shared, teardown);
  g_test_add ("/use-host-os", Fixture, NULL,
              setup, test_use_host_os, teardown);

  return g_test_run ();
}
