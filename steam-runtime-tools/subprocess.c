/*
 * Copyright © 2023 Collabora Ltd.
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
