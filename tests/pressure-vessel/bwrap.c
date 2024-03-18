/*
 * Copyright © 2019-2023 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "steam-runtime-tools/env-overlay-internal.h"
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx.h"

#include "tests/test-utils.h"
#include "bwrap.h"

typedef struct
{
  SrtEnvOverlay *container_env;
} Fixture;

typedef struct
{
  int unused;
} Config;

static const char * const initial_envp[] =
{
  "FLATPAK_ID=com.valvesoftware.Steam",
  "G_MESSAGES_DEBUG=",
  "LD_AUDIT=audit.so",
  "LD_PRELOAD=libfakeroot.so",
  "STEAM_RUNTIME=0",
  "TMPDIR=/tmp",
  NULL
};

static void
setup (Fixture *f,
       gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  f->container_env = _srt_env_overlay_new ();

  /* In each of these pairs, the first one is filtered by glibc and the
   * second is not. */
  _srt_env_overlay_set (f->container_env, "LD_AUDIT", "audit.so");
  _srt_env_overlay_set (f->container_env, "G_MESSAGES_DEBUG", "all");
  _srt_env_overlay_set (f->container_env, "TMPDIR", NULL);
  _srt_env_overlay_set (f->container_env, "STEAM_RUNTIME", NULL);
  _srt_env_overlay_inherit (f->container_env, "LD_PRELOAD");
  _srt_env_overlay_inherit (f->container_env, "FLATPAK_ID");
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  g_clear_pointer (&f->container_env, _srt_env_overlay_unref);
}

static void
dump_bwrap (FlatpakBwrap *bwrap,
            const char *label)
{
  gsize i;

  g_test_message ("%s:", label);

  for (i = 0; i < bwrap->argv->len; i++)
    {
      const char *arg = g_ptr_array_index (bwrap->argv, i);

      g_test_message ("\ta: %s", arg);
    }

  for (i = 0; bwrap->envp != NULL && bwrap->envp[i] != NULL; i++)
    {
      const char *e = bwrap->envp[i];

      g_test_message ("\te: %s", e);
    }
}

/*
 * Assert that item @i in the array @strv is @expected.
 */
static void
assert_1_item (const char * const *strv,
               gsize i,
               const char *expected)
{
  g_assert_cmpstr (strv[i], ==, expected);
}

static void
dump_env0 (GBytes *env0)
{
  size_t len;
  const char *data = g_bytes_get_data (env0, &len);

  g_test_message ("env -0: %zu bytes", len);

  while (len > 0)
    {
      size_t next_len = strnlen (data, len);

      g_assert_cmpuint (next_len, <, len);
      g_test_message ("\t%s", data);
      data += (next_len + 1);
      len -= (next_len + 1);
    }
}

/*
 * Assert that argument @i is @expected.
 */
static void
assert_fd_with_payload (FlatpakBwrap *bwrap,
                        gsize i,
                        const void *expected,
                        size_t expected_len,
                        void (*dump_function) (GBytes *))
{
  g_autoptr(GBytes) bytes = NULL;
  g_autoptr(GError) local_error = NULL;
  const char *fd_str;
  guint64 fd;
  char *endptr;
  const void *actual;
  size_t actual_len = 0;

  g_assert_cmpuint (bwrap->argv->len, >=, i + 1);
  fd_str = g_ptr_array_index (bwrap->argv, i);
  fd = g_ascii_strtoull (fd_str, &endptr, 10);
  /* Assume stdin, stdout, stderr are already in use */
  g_assert_cmpuint (fd, >, 2);
  g_assert_cmpuint (fd, <=, G_MAXINT);
  g_assert_cmpint (*endptr, ==, '\0');
  g_assert_true (endptr != (char *) fd_str);

  (void) lseek ((int) fd, 0, SEEK_SET);
  bytes = glnx_fd_readall_bytes ((int) fd, NULL, &local_error);
  g_assert_no_error (local_error);
  g_assert_nonnull (bytes);

  if (dump_function != NULL)
    dump_function (bytes);

  actual = g_bytes_get_data (bytes, &actual_len);
  g_assert_cmpmem (expected, expected_len, actual, actual_len);
}

/*
 * Assert that argument @i is @expected.
 */
static void
assert_1_arg (FlatpakBwrap *bwrap,
              gsize i,
              const char *expected)
{
  g_assert_cmpuint (bwrap->argv->len, >=, i + 1);
  assert_1_item ((const char * const *) bwrap->argv->pdata, i, expected);
}

/*
 * Assert that 2 arguments starting from @i are (a, b).
 * Return the index of the next argument, either in-bounds for the array
 * or 1 more than the last valid argument.
 */
static gsize
assert_2_args (FlatpakBwrap *bwrap,
               gsize i,
               const char *a,
               const char *b)
{
  g_assert_cmpuint (bwrap->argv->len, >=, i + 2);
  assert_1_arg (bwrap, i, a);
  assert_1_arg (bwrap, i + 1, b);
 return i + 2;
}

/*
 * Assert that 3 arguments starting from @i are (a, b, c).
 * Return the index of the next argument, either in-bounds for the array
 * or 1 more than the last valid argument.
 */
static gsize
assert_3_args (FlatpakBwrap *bwrap,
               gsize i,
               const char *a,
               const char *b,
               const char *c)
{
  g_assert_cmpuint (bwrap->argv->len, >=, i + 3);
  assert_1_arg (bwrap, i, a);
  assert_1_arg (bwrap, i + 1, b);
  assert_1_arg (bwrap, i + 2, c);
  return i + 3;
}

/* This is the normal code path when Flatpak is not involved */
static void
test_from_container_env (Fixture *f,
                         gconstpointer context)
{
  g_autoptr(FlatpakBwrap) bwrap_argv = flatpak_bwrap_new ((char **) initial_envp);
  g_autoptr(FlatpakBwrap) bwrap_envp = flatpak_bwrap_new ((char **) initial_envp);
  g_auto(GStrv) envp = NULL;
  gsize i;

  pv_bwrap_container_env_to_envp (bwrap_envp, f->container_env);
  dump_bwrap (bwrap_envp, "Environment for final command");
  pv_bwrap_filtered_container_env_to_bwrap_argv (bwrap_argv, f->container_env);
  dump_bwrap (bwrap_argv, "Arguments to add to bwrap");
  /*
   * Set variable => set variable in envp, and also add --setenv if it's one
   *  that glibc would otherwise filter out in a setuid bwrap
   * Explicitly unset variable => remove variable from envp
   * Inherited variable => no action
   */
  g_assert_cmpuint (0, ==, bwrap_envp->argv->len);
  g_assert_nonnull (bwrap_envp->envp);
  g_assert_nonnull (bwrap_envp->envp[0]);
  envp = g_strdupv (bwrap_envp->envp);
  qsort (envp, g_strv_length (envp), sizeof (char *), _srt_indirect_strcmp0);
  i = 0;
  assert_1_item (_srt_const_strv (envp), i++, "FLATPAK_ID=com.valvesoftware.Steam");
  assert_1_item (_srt_const_strv (envp), i++, "G_MESSAGES_DEBUG=all");
  assert_1_item (_srt_const_strv (envp), i++, "LD_AUDIT=audit.so");
  assert_1_item (_srt_const_strv (envp), i++, "LD_PRELOAD=libfakeroot.so");
  assert_1_item (_srt_const_strv (envp), i++, NULL);

  g_assert_cmpstrv (bwrap_argv->envp, initial_envp);
  i = 0;
  i = assert_3_args (bwrap_argv, i, "--setenv", "LD_AUDIT", "audit.so");
  g_assert_cmpuint (i, ==, bwrap_argv->argv->len);
}

/* This is the code path we take if starting a Flatpak subsandbox */
static void
test_from_container_env_subsandbox (Fixture *f,
                                    gconstpointer context)
{
  static const char expected_env[] = (
    "G_MESSAGES_DEBUG=all\0"
    "LD_AUDIT=audit.so\0"
    /* ... plus an implicit \0 */
  );
  g_autoptr(GError) local_error = NULL;
  g_autoptr(FlatpakBwrap) flatpak_subsandbox = flatpak_bwrap_new ((char **) initial_envp);
  gboolean ok;
  gsize i;

  ok = pv_bwrap_container_env_to_subsandbox_argv (flatpak_subsandbox,
                                                  f->container_env,
                                                  &local_error);
  g_assert_no_error (local_error);
  g_assert_true (ok);
  dump_bwrap (flatpak_subsandbox, "Arguments to add to s-r-launch-client");
  /*
   * Set variable => --env-fd
   * Explicitly unset variable => --unset-env
   * Inherited variable => no action
   * envp is untouched.
   */
  g_assert_cmpstrv (flatpak_subsandbox->envp, initial_envp);
  i = 0;
  assert_1_arg (flatpak_subsandbox, i++, "--env-fd");
  assert_fd_with_payload (flatpak_subsandbox, i++,
                          expected_env, sizeof (expected_env) - 1,
                          dump_env0);
  i = assert_2_args (flatpak_subsandbox, i, "--unset-env", "STEAM_RUNTIME");
  i = assert_2_args (flatpak_subsandbox, i, "--unset-env", "TMPDIR");
  g_assert_cmpuint (i, ==, flatpak_subsandbox->argv->len);
}

int
main (int argc,
      char **argv)
{
  _srt_setenv_disable_gio_modules ();

  _srt_tests_init (&argc, &argv, NULL);
  g_test_add ("/bwrap/from-container-env/flatpak-subsandbox", Fixture, NULL,
              setup, test_from_container_env_subsandbox, teardown);
  g_test_add ("/bwrap/from-container-env/normal", Fixture, NULL,
              setup, test_from_container_env, teardown);

  return g_test_run ();
}
