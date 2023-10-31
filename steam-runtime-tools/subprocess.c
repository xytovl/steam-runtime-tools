/*
 * Copyright © 2019-2023 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "steam-runtime-tools/subprocess-internal.h"

#include "steam-runtime-tools/enums.h"
#include "steam-runtime-tools/utils-internal.h"

struct _SrtSubprocessRunner
{
  GObject parent;
  /* Environment */
  GStrv envp;
  /* Path to find helper executables, or %NULL to use $SRT_HELPERS_PATH
   * or the installed helpers */
  gchar *helpers_path;
  SrtTestFlags test_flags;
};

struct _SrtSubprocessRunnerClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE (SrtSubprocessRunner, _srt_subprocess_runner, G_TYPE_OBJECT)

enum
{
  PROP_0,
  PROP_ENVIRON,
  PROP_HELPERS_PATH,
  PROP_TEST_FLAGS,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL };

static void
_srt_subprocess_runner_init (SrtSubprocessRunner *self)
{
}

static void
_srt_subprocess_runner_constructed (GObject *object)
{
  SrtSubprocessRunner *self = SRT_SUBPROCESS_RUNNER (object);

  if (self->envp == NULL)
    self->envp = g_get_environ ();

  G_OBJECT_CLASS (_srt_subprocess_runner_parent_class)->constructed (object);
}

static void
_srt_subprocess_runner_get_property (GObject *object,
                                     guint prop_id,
                                     GValue *value,
                                     GParamSpec *pspec)
{
  SrtSubprocessRunner *self = SRT_SUBPROCESS_RUNNER (object);

  switch (prop_id)
    {
      case PROP_ENVIRON:
        g_value_set_boxed (value, self->envp);
        break;

      case PROP_HELPERS_PATH:
        g_value_set_string (value, self->helpers_path);
        break;

      case PROP_TEST_FLAGS:
        g_value_set_flags (value, self->test_flags);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
_srt_subprocess_runner_set_property (GObject *object,
                                     guint prop_id,
                                     const GValue *value,
                                     GParamSpec *pspec)
{
  SrtSubprocessRunner *self = SRT_SUBPROCESS_RUNNER (object);

  switch (prop_id)
    {
      case PROP_ENVIRON:
        /* Construct-only */
        g_return_if_fail (self->envp == NULL);
        self->envp = g_value_dup_boxed (value);
        break;

      case PROP_HELPERS_PATH:
        /* Construct-only */
        g_return_if_fail (self->helpers_path == NULL);
        self->helpers_path = g_value_dup_string (value);
        break;

      case PROP_TEST_FLAGS:
        /* Construct-only */
        g_return_if_fail (self->test_flags == SRT_TEST_FLAGS_NONE);
        self->test_flags = g_value_get_flags (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
_srt_subprocess_runner_finalize (GObject *object)
{
  SrtSubprocessRunner *self = SRT_SUBPROCESS_RUNNER (object);

  g_strfreev (self->envp);
  g_free (self->helpers_path);

  G_OBJECT_CLASS (_srt_subprocess_runner_parent_class)->finalize (object);
}

static void
_srt_subprocess_runner_class_init (SrtSubprocessRunnerClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->constructed = _srt_subprocess_runner_constructed;
  object_class->get_property = _srt_subprocess_runner_get_property;
  object_class->set_property = _srt_subprocess_runner_set_property;
  object_class->finalize = _srt_subprocess_runner_finalize;

  properties[PROP_ENVIRON] =
    g_param_spec_boxed ("environ", NULL, NULL, G_TYPE_STRV,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);
  properties[PROP_HELPERS_PATH] =
    g_param_spec_string ("helpers-path", NULL, NULL, NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);
  properties[PROP_TEST_FLAGS] =
    g_param_spec_flags ("test-flags", NULL, NULL,
                        SRT_TYPE_TEST_FLAGS, SRT_TEST_FLAGS_NONE,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

/*
 * Returns: The environment. Never %NULL unless a programming error occurs.
 */
const char * const *
_srt_subprocess_runner_get_environ (SrtSubprocessRunner *self)
{
  g_return_val_if_fail (SRT_IS_SUBPROCESS_RUNNER (self), NULL);
  return (const char * const *) self->envp;
}

/*
 * Returns: The value of environment variable @var, or %NULL if unset.
 */
const char *
_srt_subprocess_runner_getenv (SrtSubprocessRunner *self,
                               const char *var)
{
  g_return_val_if_fail (SRT_IS_SUBPROCESS_RUNNER (self), NULL);
  return g_environ_getenv (self->envp, var);
}

/*
 * Returns: The path to `x86_64-linux-gnu-check-gl` and so on,
 *  or %NULL to use a default.
 */
const char *
_srt_subprocess_runner_get_helpers_path (SrtSubprocessRunner *self)
{
  g_return_val_if_fail (SRT_IS_SUBPROCESS_RUNNER (self), NULL);
  return self->helpers_path;
}

/*
 * Returns: Test flags
 */
SrtTestFlags
_srt_subprocess_runner_get_test_flags (SrtSubprocessRunner *self)
{
  g_return_val_if_fail (SRT_IS_SUBPROCESS_RUNNER (self), SRT_TEST_FLAGS_NONE);
  return self->test_flags;
}

/*
 * _srt_subprocess_runner_get_helper:
 * @runner: The execution environment
 * @multiarch: (nullable): A multiarch tuple like %SRT_ABI_I386 to prefix
 *  to the executable name, or %NULL
 * @base: (not nullable): Base name of the executable
 * @flags: Flags affecting how we set up the helper
 * @error: Used to raise an error if %NULL is returned
 *
 * Find a helper executable. We return an array of arguments so that the
 * helper can be wrapped by an "adverb" like `env`, `timeout` or a
 * specific `ld.so` implementation if required.
 *
 * Returns: (nullable) (element-type filename) (transfer container): The
 *  initial `argv` for the helper, with g_free() set as the free-function, and
 *  no %NULL terminator. Free with g_ptr_array_unref() or g_ptr_array_free().
 */
GPtrArray *
_srt_subprocess_runner_get_helper (SrtSubprocessRunner *self,
                                   const char *multiarch,
                                   const char *base,
                                   SrtHelperFlags flags,
                                   GError **error)
{
  const char *helpers_path;
  GPtrArray *argv = NULL;
  gchar *path;
  gchar *prefixed;

  g_return_val_if_fail (_srt_check_not_setuid (), NULL);
  g_return_val_if_fail (base != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  argv = g_ptr_array_new_with_free_func (g_free);

  if (g_getenv ("SNAP_DESKTOP_RUNTIME") != NULL)
    {
      g_debug ("Working around https://github.com/canonical/steam-snap/issues/17 by not using timeout(1)");
    }
  else if (flags & SRT_HELPER_FLAGS_TIME_OUT)
    {
      g_ptr_array_add (argv, g_strdup ("timeout"));
      g_ptr_array_add (argv, g_strdup ("-s"));
      g_ptr_array_add (argv, g_strdup ("TERM"));

      if (self->test_flags & SRT_TEST_FLAGS_TIME_OUT_SOONER)
        {
          /* Speed up the failing case in automated testing */
          g_ptr_array_add (argv, g_strdup ("-k"));
          g_ptr_array_add (argv, g_strdup ("1"));
          g_ptr_array_add (argv, g_strdup ("1"));
        }
      else
        {
          /* Kill the helper (if still running) 3 seconds after the TERM
           * signal */
          g_ptr_array_add (argv, g_strdup ("-k"));
          g_ptr_array_add (argv, g_strdup ("3"));
          /* Send TERM signal after 10 seconds */
          g_ptr_array_add (argv, g_strdup ("10"));
        }
    }

  helpers_path = self->helpers_path;

  if (helpers_path == NULL)
    helpers_path = g_getenv ("SRT_HELPERS_PATH");

  if (helpers_path == NULL
      && _srt_find_myself (&helpers_path, error) == NULL)
    {
      g_ptr_array_unref (argv);
      return NULL;
    }

  /* Prefer a helper from ${SRT_HELPERS_PATH} or
   * ${libexecdir}/steam-runtime-tools-${_SRT_API_MAJOR}
   * if it exists */
  path = g_strdup_printf ("%s/%s%s%s",
                          helpers_path,
                          multiarch == NULL ? "" : multiarch,
                          multiarch == NULL ? "" : "-",
                          base);

  g_debug ("Looking for %s", path);

  if (g_file_test (path, G_FILE_TEST_IS_EXECUTABLE))
    {
      g_ptr_array_add (argv, g_steal_pointer (&path));
      return argv;
    }

  if ((flags & SRT_HELPER_FLAGS_SEARCH_PATH) == 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "%s not found", path);
      g_free (path);
      g_ptr_array_unref (argv);
      return NULL;
    }

  /* For helpers that are not part of steam-runtime-tools
   * (historically this included *-wflinfo), we fall back to searching $PATH */
  g_free (path);

  if (multiarch == NULL)
    prefixed = g_strdup (base);
  else
    prefixed = g_strdup_printf ("%s-%s", multiarch, base);

  g_ptr_array_add (argv, g_steal_pointer (&prefixed));
  return argv;
}

gboolean
_srt_subprocess_runner_spawn_sync (SrtSubprocessRunner *self,
                                   SrtHelperFlags flags,
                                   const char * const *argv,
                                   gchar **stdout_out,
                                   gchar **stderr_out,
                                   gint *wait_status_out,
                                   GError **error)
{
  g_auto(GStrv) my_environ = NULL;
  GSpawnFlags spawn_flags = G_SPAWN_DEFAULT;

  if (!(flags & SRT_HELPER_FLAGS_KEEP_GAMEOVERLAYRENDERER))
    my_environ = _srt_filter_gameoverlayrenderer_from_envp ((const char * const *) self->envp);

  if (flags & SRT_HELPER_FLAGS_LIBGL_VERBOSE)
    {
      if (my_environ == NULL)
        my_environ = g_strdupv (self->envp);

      my_environ = g_environ_setenv (my_environ, "LIBGL_DEBUG", "verbose", TRUE);
    }

  /* When prepending timeout(1) to argv, we always need to search PATH */
  if (flags & (SRT_HELPER_FLAGS_TIME_OUT | SRT_HELPER_FLAGS_SEARCH_PATH))
    spawn_flags |= G_SPAWN_SEARCH_PATH;

  if (flags & SRT_HELPER_FLAGS_STDOUT_SILENCE)
    {
      g_return_val_if_fail (stdout_out == NULL, FALSE);
      spawn_flags |= G_SPAWN_STDOUT_TO_DEV_NULL;
    }

  return g_spawn_sync (NULL,        /* working directory */
                       (gchar **) argv,
                       my_environ != NULL ? my_environ : self->envp,
                       spawn_flags,
                       _srt_child_setup_unblock_signals,
                       NULL,        /* user data */
                       stdout_out,
                       stderr_out,
                       wait_status_out,
                       error);
}
