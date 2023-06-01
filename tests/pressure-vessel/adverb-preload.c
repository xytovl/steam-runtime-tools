/*
 * Copyright © 2023 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <glib.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx.h"

#include "adverb-preload.h"
#include "flatpak-utils-base-private.h"
#include "tests/test-utils.h"

typedef struct
{
  TestsOpenFdSet old_fds;
  FlatpakBwrap *bwrap;
  PvPerArchDirs *lib_temp_dirs;
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
  g_autoptr(GError) local_error = NULL;
  gsize i;

  f->old_fds = tests_check_fd_leaks_enter ();
  f->bwrap = flatpak_bwrap_new (flatpak_bwrap_empty_env);
  f->lib_temp_dirs = pv_per_arch_dirs_new (&local_error);

  if (f->lib_temp_dirs == NULL)
    {
      g_test_skip (local_error->message);
      return;
    }

  g_test_message ("Cross-platform module prefix: %s",
                  f->lib_temp_dirs->libdl_token_path);

  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
    g_test_message ("Concrete path for %s architecture: %s",
                    pv_multiarch_tuples[i],
                    f->lib_temp_dirs->abi_paths[i]);
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  g_clear_pointer (&f->lib_temp_dirs, pv_per_arch_dirs_free);
  g_clear_pointer (&f->bwrap, flatpak_bwrap_free);
  tests_check_fd_leaks_leave (f->old_fds);
}

static void
test_basic (Fixture *f,
            gconstpointer context)
{
  static const PvAdverbPreloadModule modules[] =
  {
    { (char *) "", PV_PRELOAD_VARIABLE_INDEX_LD_AUDIT, 0 },
    { (char *) "/opt/libaudit.so", PV_PRELOAD_VARIABLE_INDEX_LD_AUDIT, 0 },
    { (char *) "", PV_PRELOAD_VARIABLE_INDEX_LD_AUDIT, PV_UNSPECIFIED_ABI },
    { (char *) "/opt/libpreload.so", PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, 0 },
    { (char *) "/opt/unspecified.so", PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      PV_UNSPECIFIED_ABI },
    { (char *) "/opt/libpreload2.so", PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, 0 },
    { (char *) "/opt/unspecified2.so", PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
      PV_UNSPECIFIED_ABI },
  };
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GString) expected = g_string_new ("");
  g_autoptr(GString) path = g_string_new ("");
  gboolean ret;
  gsize i;

  if (f->lib_temp_dirs == NULL)
    return;

  ret = pv_adverb_set_up_preload_modules (f->bwrap,
                                          f->lib_temp_dirs,
                                          modules,
                                          G_N_ELEMENTS (modules),
                                          &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);

  flatpak_bwrap_sort_envp (f->bwrap);
  g_assert_nonnull (f->bwrap->envp);
  i = 0;

  g_string_assign (expected, "LD_AUDIT=");
  g_string_append_printf (expected, "%s/libaudit.so",
                          f->lib_temp_dirs->libdl_token_path);
  g_assert_cmpstr (f->bwrap->envp[i], ==, expected->str);
  i++;

  /* Order is preserved, independent of whether an ABI is specified */
  g_string_assign (expected, "LD_PRELOAD=");
  g_string_append_printf (expected, "%s/libpreload.so",
                          f->lib_temp_dirs->libdl_token_path);
  g_string_append_c (expected, ':');
  g_string_append (expected, "/opt/unspecified.so");
  g_string_append_c (expected, ':');
  g_string_append_printf (expected, "%s/libpreload2.so",
                          f->lib_temp_dirs->libdl_token_path);
  g_string_append_c (expected, ':');
  g_string_append (expected, "/opt/unspecified2.so");
  g_assert_cmpstr (f->bwrap->envp[i], ==, expected->str);
  i++;

  g_assert_cmpstr (f->bwrap->envp[i], ==, NULL);

  for (i = 0; i < G_N_ELEMENTS (modules); i++)
    {
      g_autofree gchar *target = NULL;

      /* Empty module entries are ignored */
      if (modules[i].name[0] == '\0')
        continue;

      g_string_assign (path, f->lib_temp_dirs->abi_paths[0]);
      g_string_append_c (path, G_DIR_SEPARATOR);
      g_string_append (path, glnx_basename (modules[i].name));
      target = flatpak_readlink (path->str, &local_error);

      /* Only the modules that have architecture-specific variations
       * (in practice those that originally had $LIB or $PLATFORM) need
       * symlinks created for them, because only those modules get their
       * LD_PRELOAD entries rewritten */
      if (modules[i].abi_index == 0)
        {
          g_assert_no_error (local_error);
          g_test_message ("%s -> %s", path->str, target);
          g_assert_cmpstr (target, ==, modules[i].name);
        }
      else
        {
          g_assert_error (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
          g_clear_error (&local_error);
        }
    }
}

static void
test_biarch (Fixture *f,
             gconstpointer context)
{
#if PV_N_SUPPORTED_ARCHITECTURES >= 2
  static const PvAdverbPreloadModule modules[] =
  {
    { (char *) "/opt/libpreload.so", PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, PV_UNSPECIFIED_ABI },
    /* In practice x86_64-linux-gnu */
    { (char *) "/opt/lib0/libpreload.so", PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, 0 },
    /* In practice i386-linux-gnu */
    { (char *) "/opt/lib1/libpreload.so", PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, 1 },
  };
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GString) expected = g_string_new ("");
  g_autoptr(GString) path = g_string_new ("");
  gboolean ret;
  gsize i;

  if (f->lib_temp_dirs == NULL)
    return;

  ret = pv_adverb_set_up_preload_modules (f->bwrap,
                                          f->lib_temp_dirs,
                                          modules,
                                          G_N_ELEMENTS (modules),
                                          &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);

  flatpak_bwrap_sort_envp (f->bwrap);
  g_assert_nonnull (f->bwrap->envp);
  i = 0;

  /* We don't have any LD_AUDIT modules in this example, so we don't set
   * those up at all, and therefore we expect f->bwrap->envp not to
   * contain LD_AUDIT. */

  g_string_assign (expected, "LD_PRELOAD=");
  g_string_append (expected, "/opt/libpreload.so");
  g_string_append_c (expected, ':');
  g_string_append_printf (expected, "%s/libpreload.so",
                          f->lib_temp_dirs->libdl_token_path);
  g_assert_cmpstr (f->bwrap->envp[i], ==, expected->str);
  i++;

  g_assert_cmpstr (f->bwrap->envp[i], ==, NULL);

  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
    {
      g_autofree gchar *target = NULL;

      g_string_assign (path, f->lib_temp_dirs->abi_paths[i]);
      g_string_append_c (path, G_DIR_SEPARATOR);
      g_string_append (path, "libpreload.so");

      target = flatpak_readlink (path->str, &local_error);
      g_assert_no_error (local_error);
      g_test_message ("%s -> %s", path->str, target);

      g_string_assign (expected, "");
      g_string_append_printf (expected, "/opt/lib%zu/libpreload.so", i);
      g_assert_cmpstr (target, ==, expected->str);
    }
#else
  /* In practice this is reached on non-x86 */
  g_test_skip ("Biarch libraries not supported on this architecture");
#endif
}

/*
 * There is a special case for gameoverlayrenderer.so:
 * pv-adverb --ld-preload=/.../ubuntu12_32/gameoverlayrenderer.so is
 * treated as if it had been .../gameoverlayrenderer.so:abi=i386-linux-gnu,
 * and so on.
 */
static void
test_gameoverlayrenderer (Fixture *f,
                          gconstpointer context)
{
#if defined(__x86_64__) || defined(__i386__)
  static const PvAdverbPreloadModule modules[] =
  {
    { (char *) "/opt/steam/some-other-abi/gameoverlayrenderer.so",
      PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, PV_UNSPECIFIED_ABI },
    { (char *) "/opt/steam/ubuntu12_32/gameoverlayrenderer.so",
      PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, PV_UNSPECIFIED_ABI },
    { (char *) "/opt/steam/ubuntu12_64/gameoverlayrenderer.so",
      PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, PV_UNSPECIFIED_ABI },
    { (char *) "/opt/steam/some-other-abi/gameoverlayrenderer.so",
      PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, PV_UNSPECIFIED_ABI },
  };
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GString) expected = g_string_new ("");
  g_autoptr(GString) path = g_string_new ("");
  gboolean ret;
  gsize i;

  G_STATIC_ASSERT (PV_N_SUPPORTED_ARCHITECTURES == 2);

  if (f->lib_temp_dirs == NULL)
    return;

  ret = pv_adverb_set_up_preload_modules (f->bwrap,
                                          f->lib_temp_dirs,
                                          modules,
                                          G_N_ELEMENTS (modules),
                                          &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);

  flatpak_bwrap_sort_envp (f->bwrap);
  g_assert_nonnull (f->bwrap->envp);
  i = 0;

  g_string_assign (expected, "LD_PRELOAD=");
  g_string_append (expected, "/opt/steam/some-other-abi/gameoverlayrenderer.so");
  g_string_append_c (expected, ':');
  g_string_append_printf (expected, "%s/gameoverlayrenderer.so",
                          f->lib_temp_dirs->libdl_token_path);
  g_string_append_c (expected, ':');
  g_string_append (expected, "/opt/steam/some-other-abi/gameoverlayrenderer.so");
  g_assert_cmpstr (f->bwrap->envp[i], ==, expected->str);
  i++;

  g_assert_cmpstr (f->bwrap->envp[i], ==, NULL);

  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
    {
      g_autofree gchar *target = NULL;

      g_string_assign (path, f->lib_temp_dirs->abi_paths[i]);
      g_string_append_c (path, G_DIR_SEPARATOR);
      g_string_append (path, "gameoverlayrenderer.so");

      target = flatpak_readlink (path->str, &local_error);
      g_assert_no_error (local_error);
      g_test_message ("%s -> %s", path->str, target);

      g_string_assign (expected, "");
      g_string_append_printf (expected, "/opt/steam/%s/gameoverlayrenderer.so",
                              pv_multiarch_details[i].gameoverlayrenderer_dir);
      g_assert_cmpstr (target, ==, expected->str);
    }
#else
  g_test_skip ("gameoverlayrenderer special-case is only implemented on x86");
#endif
}

/*
 * steamrt/tasks#302: pv-adverb would fail if /usr/$LIB/libMangoHud.so
 * was (uselessly) added to the LD_PRELOAD path more than once.
 * This test exercises the same thing for gameoverlayrenderer.so, too.
 */
static void
test_repetition (Fixture *f,
                 gconstpointer context)
{
  static const PvAdverbPreloadModule modules[] =
  {
    { (char *) "/opt/lib0/libfirst.so", PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, 0 },
    { (char *) "/opt/lib0/one/same-basename.so",
      PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, 0 },
    { (char *) "/opt/lib0/two/same-basename.so",
      PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, 0 },
    { (char *) "/opt/lib0/libpreload.so", PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, 0 },
#if PV_N_SUPPORTED_ARCHITECTURES > 1
    { (char *) "/opt/lib1/libpreload.so", PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, 1 },
#endif
#if defined(__x86_64__) || defined(__i386__)
    { (char *) "/opt/steam/ubuntu12_32/gameoverlayrenderer.so",
      PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, PV_UNSPECIFIED_ABI },
    { (char *) "/opt/steam/ubuntu12_64/gameoverlayrenderer.so",
      PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, PV_UNSPECIFIED_ABI },
#endif
    { (char *) "/opt/lib0/libmiddle.so", PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, 0 },
    { (char *) "/opt/lib0/libpreload.so", PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, 0 },
#if PV_N_SUPPORTED_ARCHITECTURES > 1
    { (char *) "/opt/lib1/libpreload.so", PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, 1 },
#endif
#if defined(__x86_64__) || defined(__i386__)
    { (char *) "/opt/steam/ubuntu12_32/gameoverlayrenderer.so",
      PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, PV_UNSPECIFIED_ABI },
    { (char *) "/opt/steam/ubuntu12_64/gameoverlayrenderer.so",
      PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, PV_UNSPECIFIED_ABI },
#endif
    { (char *) "/opt/lib0/liblast.so", PV_PRELOAD_VARIABLE_INDEX_LD_PRELOAD, 0 },
  };
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GString) expected = g_string_new ("");
  g_autoptr(GString) path = g_string_new ("");
  gboolean ret;
  gsize i;

  if (f->lib_temp_dirs == NULL)
    return;

  ret = pv_adverb_set_up_preload_modules (f->bwrap,
                                          f->lib_temp_dirs,
                                          modules,
                                          G_N_ELEMENTS (modules),
                                          &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ret);

  flatpak_bwrap_sort_envp (f->bwrap);
  g_assert_nonnull (f->bwrap->envp);
  i = 0;

  g_string_assign (expected, "LD_PRELOAD=");
  g_string_append_printf (expected, "%s/libfirst.so",
                          f->lib_temp_dirs->libdl_token_path);
  g_string_append_c (expected, ':');
  g_string_append_printf (expected, "%s/same-basename.so",
                          f->lib_temp_dirs->libdl_token_path);
  g_string_append_c (expected, ':');
  /* We don't do the per-architecture split if there's a basename
   * collision */
  g_string_append (expected, "/opt/lib0/two/same-basename.so");
  g_string_append_c (expected, ':');
  g_string_append_printf (expected, "%s/libpreload.so",
                          f->lib_temp_dirs->libdl_token_path);
#if defined(__x86_64__) || defined(__i386__)
  g_string_append_c (expected, ':');
  g_string_append_printf (expected, "%s/gameoverlayrenderer.so",
                          f->lib_temp_dirs->libdl_token_path);
#endif
  g_string_append_c (expected, ':');
  g_string_append_printf (expected, "%s/libmiddle.so",
                          f->lib_temp_dirs->libdl_token_path);
  g_string_append_c (expected, ':');
  /* The duplicates don't appear in the search path a second time */
  g_string_append_printf (expected, "%s/liblast.so",
                          f->lib_temp_dirs->libdl_token_path);
  g_assert_cmpstr (f->bwrap->envp[i], ==, expected->str);
  i++;

  g_assert_cmpstr (f->bwrap->envp[i], ==, NULL);

  /* The symlinks get created (but only once) */

  for (i = 0; i < MIN (PV_N_SUPPORTED_ARCHITECTURES, 2); i++)
    {
      gsize j;

      for (j = 0; j < G_N_ELEMENTS (modules); j++)
        {
          g_autofree gchar *target = NULL;

          if (modules[j].abi_index != i)
            {
              g_test_message ("Not expecting a %s symlink for %s",
                              pv_multiarch_tuples[i], modules[j].name);
              continue;
            }

          if (g_str_equal (modules[j].name, "/opt/lib0/two/same-basename.so"))
            {
              g_test_message ("Not expecting a symlink for %s because it "
                              "collides with a basename seen earlier",
                              modules[j].name);
              continue;
            }

          g_string_assign (path, f->lib_temp_dirs->abi_paths[i]);
          g_string_append_c (path, G_DIR_SEPARATOR);
          g_string_append (path, glnx_basename (modules[j].name));

          target = flatpak_readlink (path->str, &local_error);
          g_assert_no_error (local_error);
          g_test_message ("%s -> %s", path->str, target);

          g_assert_cmpstr (target, ==, modules[j].name);
        }
    }

#if defined(__x86_64__) || defined(__i386__)
  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
    {
      g_autofree gchar *target = NULL;

      g_string_assign (path, f->lib_temp_dirs->abi_paths[i]);
      g_string_append_c (path, G_DIR_SEPARATOR);
      g_string_append (path, "gameoverlayrenderer.so");

      target = flatpak_readlink (path->str, &local_error);
      g_assert_no_error (local_error);
      g_test_message ("%s -> %s", path->str, target);

      g_string_assign (expected, "");
      g_string_append_printf (expected, "/opt/steam/%s/gameoverlayrenderer.so",
                              pv_multiarch_details[i].gameoverlayrenderer_dir);
      g_assert_cmpstr (target, ==, expected->str);
    }
#endif
}

int
main (int argc,
      char **argv)
{
  _srt_setenv_disable_gio_modules ();

  /* In unit tests it isn't always straightforward to find the real
   * ${PLATFORM}, so use a predictable mock implementation that always
   * uses PvMultiarchDetails.platforms[0] */
  g_setenv ("PRESSURE_VESSEL_TEST_STANDARDIZE_PLATFORM", "1", TRUE);

  _srt_tests_init (&argc, &argv, NULL);
  g_test_add ("/basic", Fixture, NULL,
              setup, test_basic, teardown);
  g_test_add ("/biarch", Fixture, NULL,
              setup, test_biarch, teardown);
  g_test_add ("/gameoverlayrenderer", Fixture, NULL,
              setup, test_gameoverlayrenderer, teardown);
  g_test_add ("/repetition", Fixture, NULL,
              setup, test_repetition, teardown);

  return g_test_run ();
}
