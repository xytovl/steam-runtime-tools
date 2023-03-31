/*
 * Copyright © 2023 Collabora Ltd.
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

#include "steam-runtime-tools/display-internal.h"
#include "steam-runtime-tools/display.h"

#include "steam-runtime-tools/enums.h"
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"
#include "steam-runtime-tools/json-utils-internal.h"
#include "steam-runtime-tools/xdg-portal-internal.h"
#include "steam-runtime-tools/utils.h"
#include "steam-runtime-tools/utils-internal.h"

#include <errno.h>
#include <string.h>
#include <sys/types.h>

/**
 * SECTION:display
 * @title: Display info
 * @short_description: Get information about the display server
 * @include: steam-runtime-tools/steam-runtime-tools.h
 */

struct _SrtDisplayInfo
{
  /*< private >*/
  GObject parent;
  GStrv display_environ;
  gboolean wayland_session;
  SrtDisplayWaylandIssues wayland_issues;
};

struct _SrtDisplayInfoClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum {
  PROP_0,
  PROP_DISPLAY_ENVIRON,
  PROP_WAYLAND_SESSION,
  PROP_WAYLAND_ISSUES,
  N_PROPERTIES,
};

G_DEFINE_TYPE (SrtDisplayInfo, srt_display_info, G_TYPE_OBJECT)

static void
srt_display_info_init (SrtDisplayInfo *self)
{
}

static void
srt_display_info_get_property (GObject *object,
                               guint prop_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  SrtDisplayInfo *self = SRT_DISPLAY_INFO (object);

  switch (prop_id)
    {
      case PROP_DISPLAY_ENVIRON:
        g_value_set_boxed (value, self->display_environ);
        break;

      case PROP_WAYLAND_SESSION:
        g_value_set_boolean (value, self->wayland_session);
        break;

      case PROP_WAYLAND_ISSUES:
        g_value_set_flags (value, self->wayland_issues);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_display_info_set_property (GObject *object,
                               guint prop_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  SrtDisplayInfo *self = SRT_DISPLAY_INFO (object);

  switch (prop_id)
    {
      case PROP_DISPLAY_ENVIRON:
        /* Construct-only */
        g_return_if_fail (self->display_environ == NULL);
        self->display_environ = g_value_dup_boxed (value);

        /* Guarantee non-NULL */
        if (self->display_environ == NULL)
          self->display_environ = g_new0 (gchar *, 1);

        break;

      case PROP_WAYLAND_SESSION:
        /* Construct-only */
        g_return_if_fail (self->wayland_session == FALSE);
        self->wayland_session = g_value_get_boolean (value);
        break;

      case PROP_WAYLAND_ISSUES:
        /* Construct-only */
        g_return_if_fail (self->wayland_issues == 0);
        self->wayland_issues = g_value_get_flags (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_display_info_finalize (GObject *object)
{
  SrtDisplayInfo *self = SRT_DISPLAY_INFO (object);

  g_strfreev (self->display_environ);

  G_OBJECT_CLASS (srt_display_info_parent_class)->finalize (object);
}

static GParamSpec *interface_properties[N_PROPERTIES] = { NULL };

static void
srt_display_info_class_init (SrtDisplayInfoClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_display_info_get_property;
  object_class->set_property = srt_display_info_set_property;
  object_class->finalize = srt_display_info_finalize;

  interface_properties[PROP_DISPLAY_ENVIRON] =
    g_param_spec_boxed ("display-environ", "Display environ",
                        "Environment variables relevant to the display server",
                        G_TYPE_STRV,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  interface_properties[PROP_WAYLAND_SESSION] =
    g_param_spec_boolean ("wayland-session", "Is this a Wayland session?",
                          "TRUE if this is a Wayland session",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  interface_properties[PROP_WAYLAND_ISSUES] =
    g_param_spec_flags ("wayland-issues", "Wayland issues",
                        "Problems with Wayland",
                        SRT_TYPE_DISPLAY_WAYLAND_ISSUES,
                        SRT_DISPLAY_WAYLAND_ISSUES_NONE,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPERTIES, interface_properties);
}

/**
 * _srt_check_display:
 * @envp: (not nullable): Environment variables to use
 *
 * Returns: A SrtDisplayInfo object containing the details of the check
 */
SrtDisplayInfo *
_srt_check_display (gchar **envp)
{
  GPtrArray *builder;
  g_auto(GStrv) display_environ = NULL;
  const gchar *name;
  gboolean wayland_session = FALSE;
  SrtDisplayWaylandIssues wayland_issues = SRT_DISPLAY_WAYLAND_ISSUES_NONE;
  static const gchar * const display_env[] =
  {
    "CLUTTER_BACKEND",
    "DISPLAY",
    "GDK_BACKEND",
    "QT_QPA_PLATFORM",
    "SDL_VIDEODRIVER",
    "WAYLAND_DISPLAY",
    "XDG_CURRENT_DESKTOP",
    "XDG_SESSION_CLASS",
    "XDG_SESSION_DESKTOP",
    "XDG_SESSION_TYPE",
    NULL
  };

  g_return_val_if_fail (envp != NULL, _srt_display_info_new (NULL, FALSE,
                                                             SRT_DISPLAY_WAYLAND_ISSUES_UNKNOWN));

  builder = g_ptr_array_new_with_free_func (g_free);

  for (guint i = 0; display_env[i] != NULL; i++)
    {
      const gchar *value = g_environ_getenv (envp, display_env[i]);
      if (value != NULL)
        {
          g_autofree gchar *key_value = NULL;
          key_value = g_strjoin ("=", display_env[i], value, NULL);
          g_ptr_array_add (builder, g_steal_pointer (&key_value));
        }
    }

  g_ptr_array_add (builder, NULL);
  display_environ = (gchar **) g_ptr_array_free (builder, FALSE);

  name = g_environ_getenv (envp, "WAYLAND_DISPLAY");
  /* If unset, the default fallback is `wayland-0` */
  if (name == NULL)
    name = "wayland-0";

  if (g_path_is_absolute (name))
    {
      /* Support for absolute paths has been added since Wayland 1.15 */
      if (g_file_test (name, G_FILE_TEST_EXISTS))
        wayland_session = TRUE;
      else
        wayland_issues |= SRT_DISPLAY_WAYLAND_ISSUES_MISSING_SOCKET;
    }
  else
    {
      const gchar *xdg_runtime_dir;
      xdg_runtime_dir = g_environ_getenv (envp, "XDG_RUNTIME_DIR");

      if (xdg_runtime_dir == NULL)
        {
          /* Without XDG_RUNTIME_DIR it is impossible to find the Wayland
           * socket. */
          wayland_issues |= SRT_DISPLAY_WAYLAND_ISSUES_MISSING_SOCKET;
        }
      else
        {
          g_autofree gchar *socket_path = NULL;
          socket_path = g_build_filename (xdg_runtime_dir, name, NULL);

          if (g_file_test (socket_path, G_FILE_TEST_EXISTS))
            wayland_session = TRUE;
          else
            wayland_issues |= SRT_DISPLAY_WAYLAND_ISSUES_MISSING_SOCKET;
        }
    }

  return _srt_display_info_new (display_environ, wayland_session, wayland_issues);
}

/**
 * srt_display_info_get_environment_list:
 * @self: A SrtDisplayInfo object
 *
 * Return the list of environment variables that are usually responsible to
 * affect the display server.
 *
 * Returns: (array zero-terminated=1) (transfer none) (not nullable):
 *  #SrtDisplayInfo:display_environ
 */
const gchar * const *
srt_display_info_get_environment_list (SrtDisplayInfo *self)
{
  g_return_val_if_fail (SRT_IS_DISPLAY_INFO (self), NULL);
  return (const gchar * const *) self->display_environ;
}

/**
 * srt_display_info_is_wayland_session:
 * @self: A SrtDisplayInfo object
 *
 * Return %TRUE if the current display session is using Wayland
 *
 * Returns: #SrtDisplayInfo:wayland_session
 */
gboolean
srt_display_info_is_wayland_session (SrtDisplayInfo *self)
{
  g_return_val_if_fail (SRT_IS_DISPLAY_INFO (self), FALSE);
  return self->wayland_session;
}

/**
 * srt_display_info_get_wayland_issues:
 * @self: A SrtDisplayInfo object
 *
 * Return flags indicating issues found with the Wayland session
 *
 * Returns: #SrtDisplayInfo:wayland-issues
 */
SrtDisplayWaylandIssues
srt_display_info_get_wayland_issues (SrtDisplayInfo *self)
{
  g_return_val_if_fail (SRT_IS_DISPLAY_INFO (self), SRT_DISPLAY_WAYLAND_ISSUES_UNKNOWN);
  return self->wayland_issues;
}

/**
 * _srt_display_info_get_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for the display info
 *
 * Returns: A new #SrtDisplayInfo.
 */
SrtDisplayInfo *
_srt_display_info_get_from_report (JsonObject *json_obj)
{
  SrtDisplayWaylandIssues wayland_issues = SRT_DISPLAY_WAYLAND_ISSUES_UNKNOWN;
  gboolean wayland_session = FALSE;
  GPtrArray *builder;
  g_auto(GStrv) display_environ = NULL;
  JsonObject *json_sub_obj;
  JsonArray *array;

  g_return_val_if_fail (json_obj != NULL, NULL);

  if (json_object_has_member (json_obj, "display"))
    {
      json_sub_obj = json_object_get_object_member (json_obj, "display");

      if (json_sub_obj == NULL)
        goto out;

      if (json_object_has_member (json_sub_obj, "environment"))
        {
          array = json_object_get_array_member (json_sub_obj, "environment");
          if (array == NULL)
            {
              g_debug ("'environment' in 'display' is not an array as expected");
            }
          else
            {
              guint length = json_array_get_length (array);
              builder = g_ptr_array_new_full (length + 1, g_free);

              for (guint i = 0; i < length; i++)
                {
                  const gchar *env_var =  json_array_get_string_element (array, i);
                  g_ptr_array_add (builder, g_strdup (env_var));
                }

              g_ptr_array_add (builder, NULL);
              display_environ = (gchar **) g_ptr_array_free (builder, FALSE);
            }
        }

      wayland_session = json_object_get_boolean_member_with_default (json_sub_obj,
                                                                     "wayland-session",
                                                                     FALSE);

      wayland_issues = srt_get_flags_from_json_array (SRT_TYPE_DISPLAY_WAYLAND_ISSUES,
                                                      json_sub_obj,
                                                      "wayland-issues",
                                                      SRT_DISPLAY_WAYLAND_ISSUES_UNKNOWN);
  }

out:
  return _srt_display_info_new (display_environ, wayland_session, wayland_issues);
}
