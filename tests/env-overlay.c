/*
 * Copyright © 2019-2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <glib.h>
#include <glib/gstdio.h>

#include "steam-runtime-tools/env-overlay-internal.h"
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx.h"

#include "tests/test-utils.h"

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

  /* In each of these pairs, the first one is filtered by glibc for setuid
   * executables and the second is not (although in fact this doesn't
   * matter, because we treat both cases the same here). */
  _srt_env_overlay_set (f->container_env, "LD_AUDIT", "audit2.so");
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
test_apply (Fixture *f,
            gconstpointer context)
{
  static const char * const expected[] =
  {
    "FLATPAK_ID=com.valvesoftware.Steam",   /* inherited */
    "G_MESSAGES_DEBUG=all",                 /* replaced */
    "LD_AUDIT=audit2.so",                   /* replaced */
    "LD_PRELOAD=libfakeroot.so",            /* inherited */
    /* STEAM_RUNTIME has been unset */
    /* TMPDIR has been unset */
    NULL
  };
  g_auto(GStrv) envp = NULL;

  envp = g_strdupv ((gchar **) initial_envp);
  envp = _srt_env_overlay_apply (f->container_env, envp);
  qsort (envp, g_strv_length (envp), sizeof (char *), _srt_indirect_strcmp0);
  g_assert_cmpstrv (envp, (gchar **) expected);
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

static void
test_to_env0 (Fixture *f,
              gconstpointer context)
{
  static const char * const expected[] =
  {
    "G_MESSAGES_DEBUG=all",
    "LD_AUDIT=audit2.so",
    NULL
  };
  g_autoptr(GBytes) env0 = _srt_env_overlay_to_env0 (f->container_env);
  size_t len;
  size_t i = 0;
  const char *data = g_bytes_get_data (env0, &len);

  dump_env0 (env0);

  while (len > 0)
    {
      size_t next_len = strnlen (data, len);

      g_assert_cmpuint (next_len, <, len);
      g_assert_cmpstr (data, ==, expected[i]);
      i++;
      data += (next_len + 1);
      len -= (next_len + 1);
    }

  g_assert_null (expected[i]);
}

int
main (int argc,
      char **argv)
{
  _srt_setenv_disable_gio_modules ();

  _srt_tests_init (&argc, &argv, NULL);
  g_test_add ("/env-overlay/apply", Fixture, NULL,
              setup, test_apply, teardown);
  g_test_add ("/env-overlay/to-env0", Fixture, NULL,
              setup, test_to_env0, teardown);

  return g_test_run ();
}
