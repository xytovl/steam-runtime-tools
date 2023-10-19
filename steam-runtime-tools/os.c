/*
 * Copyright © 2019 Collabora Ltd.
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

#include "steam-runtime-tools/os.h"

#include <string.h>

#include "steam-runtime-tools/glib-backports-internal.h"

#include <glib.h>

#include "steam-runtime-tools/os-internal.h"
#include "steam-runtime-tools/utils.h"
#include "steam-runtime-tools/utils-internal.h"

/**
 * SECTION:os
 * @title: Operating system info
 * @short_description: Information about the operating system
 * @include: steam-runtime-tools/steam-runtime-tools.h
 */

/*
 * os-release(5) fields that are serialized in the s-r-system-info report,
 * in the order they are serialized (which should be the order that makes
 * most sense to a reader).
 */
const char * const _srt_interesting_os_release_fields[] =
{
  "id",
  "id_like",
  "name",
  "pretty_name",
  "version_id",
  "version_codename",
  "build_id",
  "variant_id",
  "variant",
  NULL
};

struct _SrtOsInfo
{
  /*< private >*/
  GObject parent;
  GHashTable *fields;
  gchar *messages;
  gchar *source_path;

  const gchar *build_id;
  const gchar *id;
  const gchar *name;
  const gchar *pretty_name;
  const gchar *variant;
  const gchar *variant_id;
  const gchar *version_codename;
  const gchar *version_id;
  GStrv id_like;
};

struct _SrtOsInfoClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum {
  PROP_0,
  PROP_FIELDS,
  PROP_MESSAGES,
  PROP_SOURCE_PATH,
  N_PROPERTIES
};

G_DEFINE_TYPE (SrtOsInfo, srt_os_info, G_TYPE_OBJECT)

static void
srt_os_info_init (SrtOsInfo *self)
{
}

static GHashTable *
copy_str_str_hash (GHashTable *other)
{
  GHashTable *ret;
  GHashTableIter iter;
  gpointer k, v;

  ret = g_hash_table_new_full (g_str_hash, g_str_equal,
                               g_free, g_free);

  if (other != NULL)
    {
      g_hash_table_iter_init (&iter, other);

      while (g_hash_table_iter_next (&iter, &k, &v))
        g_hash_table_replace (ret, g_strdup (k), g_strdup (v));
    }

  return ret;
}

static void
srt_os_info_get_property (GObject *object,
                                      guint prop_id,
                                      GValue *value,
                                      GParamSpec *pspec)
{
  SrtOsInfo *self = SRT_OS_INFO (object);

  switch (prop_id)
    {
      case PROP_FIELDS:
        /* Deep-copy to avoid any ambiguity about mutability */
        g_value_take_boxed (value, srt_os_info_dup_fields (self));
        break;

      case PROP_MESSAGES:
        g_value_set_string (value, self->messages);
        break;

      case PROP_SOURCE_PATH:
        g_value_set_string (value, self->source_path);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_os_info_set_property (GObject *object,
                          guint prop_id,
                          const GValue *value,
                          GParamSpec *pspec)
{
  SrtOsInfo *self = SRT_OS_INFO (object);
  const char *tmp;

  switch (prop_id)
    {
      case PROP_FIELDS:
        /* Construct-only */
        g_return_if_fail (self->fields == NULL);
        /* Deep-copy to avoid any ambiguity about mutability */
        self->fields = copy_str_str_hash (g_value_get_boxed (value));
        break;

      case PROP_MESSAGES:
        /* Construct-only */
        g_return_if_fail (self->messages == NULL);
        tmp = g_value_get_string (value);

        /* Normalize the empty string (expected to be common) to NULL */
        if (tmp != NULL && tmp[0] == '\0')
          tmp = NULL;

        self->messages = g_strdup (tmp);
        break;

      case PROP_SOURCE_PATH:
        /* Construct-only */
        g_return_if_fail (self->source_path == NULL);
        self->source_path = g_value_dup_string (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static const char WHITESPACE[] = " \t\n\r";

static void
srt_os_info_constructed (GObject *object)
{
  SrtOsInfo *self = SRT_OS_INFO (object);
  const char *id_like;

  g_assert (self->fields != NULL);
  self->build_id = g_hash_table_lookup (self->fields, "BUILD_ID");
  self->id = g_hash_table_lookup (self->fields, "ID");
  self->name = g_hash_table_lookup (self->fields, "NAME");
  self->pretty_name = g_hash_table_lookup (self->fields, "PRETTY_NAME");
  self->variant = g_hash_table_lookup (self->fields, "VARIANT");
  self->variant_id = g_hash_table_lookup (self->fields, "VARIANT_ID");
  self->version_codename = g_hash_table_lookup (self->fields, "VERSION_CODENAME");
  self->version_id = g_hash_table_lookup (self->fields, "VERSION_ID");

  id_like = g_hash_table_lookup (self->fields, "ID_LIKE");

  if (id_like != NULL)
    self->id_like = g_strsplit_set (id_like, WHITESPACE, -1);

  G_OBJECT_CLASS (srt_os_info_parent_class)->constructed (object);
}

static void
srt_os_info_finalize (GObject *object)
{
  SrtOsInfo *self = SRT_OS_INFO (object);

  g_hash_table_unref (self->fields);
  g_free (self->messages);
  g_free (self->source_path);
  g_strfreev (self->id_like);

  G_OBJECT_CLASS (srt_os_info_parent_class)->finalize (object);
}

static GParamSpec *properties[N_PROPERTIES] = { NULL };

static void
srt_os_info_class_init (SrtOsInfoClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_os_info_get_property;
  object_class->set_property = srt_os_info_set_property;
  object_class->constructed = srt_os_info_constructed;
  object_class->finalize = srt_os_info_finalize;

  properties[PROP_FIELDS] =
    g_param_spec_boxed ("fields", "Fields",
                        "Unescaped fields as specified in os-release(5)",
                        G_TYPE_HASH_TABLE,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  properties[PROP_MESSAGES] =
    g_param_spec_string ("messages", "Messages",
                         "Diagnostic messages, if any",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_SOURCE_PATH] =
    g_param_spec_string ("source-path", "Source path",
                         "Path to the source of data, if any",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

/**
 * srt_os_info_dup_fields:
 * @self: The OS information
 *
 * Return a copy of the fields from os-release(5), unescaped.
 *
 * Returns: (transfer full) (element-type utf8 utf8): A copy of the fields. Free with g_hash_table_unref().
 */
GHashTable *
srt_os_info_dup_fields (SrtOsInfo *self)
{
  g_return_val_if_fail (SRT_IS_OS_INFO (self), NULL);
  return copy_str_str_hash (self->fields);
}

/**
 * srt_os_info_get_source_path:
 * @self: The OS information
 *
 * Return the path from which the OS information was derived.
 *
 * Returns: The path, which is owned by @self and must not be freed,
 *  or %NULL.
 */
const char *
srt_os_info_get_source_path (SrtOsInfo *self)
{
  g_return_val_if_fail (SRT_IS_OS_INFO (self), NULL);
  return self->source_path;
}

/**
 * srt_os_info_get_messages:
 * @self: The OS information
 *
 * Return any diagnostic messages that were produced while building @self.
 * If non-%NULL, the result will be non-empty. It might contain multiple
 * lines terminated by `\n`.
 *
 * Returns: The messages, which are owned by @self and must not be freed,
 *  or %NULL.
 */
const char *
srt_os_info_get_messages (SrtOsInfo *self)
{
  g_return_val_if_fail (SRT_IS_OS_INFO (self), NULL);
  return self->messages;
}

/**
 * srt_os_info_get_build_id:
 * @self: The OS information
 *
 * Return a machine-readable identifier for the system image used as the
 * origin for a distribution, for example `0.20190925.0`. If called
 * from inside a Steam Runtime container, return the Steam Runtime build
 * ID, which currently looks like `0.20190925.0`.
 *
 * In operating systems that do not use image-based installation, such
 * as Debian, this will be %NULL.
 *
 * This is the `BUILD_ID` from os-release(5).
 *
 * Returns: (transfer none) (type utf8): The build ID, or %NULL if not known.
 *  The string is owned by @self and must not be freed.
 */
const char *
srt_os_info_get_build_id (SrtOsInfo *self)
{
  g_return_val_if_fail (SRT_IS_OS_INFO (self), NULL);
  return self->build_id;
}

/**
 * srt_os_info_get_id:
 * @self: The OS information
 *
 * Return a lower-case machine-readable operating system identifier,
 * for example `debian` or `arch`. If called from inside a Steam Runtime
 * container, return `steamrt`.
 *
 * This is the `ID` in os-release(5). If os-release(5) is not available,
 * future versions of this library might derive a similar ID from
 * lsb_release(1).
 *
 * Returns: (transfer none) (type utf8): The OS ID, or %NULL if not known.
 *  The string is owned by @self and must not be freed.
 */
const char *
srt_os_info_get_id (SrtOsInfo *self)
{
  g_return_val_if_fail (SRT_IS_OS_INFO (self), NULL);
  return self->id;
}

/**
 * srt_os_info_get_id_like:
 * @self: The #SrtSystemInfo object
 *
 * Return an array of lower-case machine-readable operating system
 * identifiers similar to srt_os_info_get_id() describing OSs
 * that this one resembles or is derived from.
 *
 * For example, the Steam Runtime 1 'scout' is derived from Ubuntu,
 * which is itself derived from Debian, so srt_os_info_get_id_like()
 * would return `{ "debian", "ubuntu", NULL }`.
 *
 * This is the `ID_LIKE` field from os-release(5), split into its
 * individual IDs.
 *
 * To check whether the OS is based on a particular distribution,
 * for example Ubuntu, it is necesssary to call both srt_os_info_get_id()
 * and srt_os_info_get_id_like().
 *
 * Returns: (array zero-terminated=1) (transfer none) (element-type utf8) (nullable): An
 *  array of OS IDs, or %NULL if nothing is known.
 *  The data is owned by @self and must not be freed.
 */
const char * const *
srt_os_info_get_id_like (SrtOsInfo *self)
{
  g_return_val_if_fail (SRT_IS_OS_INFO (self), NULL);
  return (const char * const *) self->id_like;
}

/**
 * srt_os_info_get_name:
 * @self: The OS information
 *
 * Return a human-readable identifier for the operating system without
 * its version, for example `Debian GNU/Linux` or `Arch Linux`.
 *
 * This is the `NAME` in os-release(5). If os-release(5) is not
 * available, future versions of this library might derive a similar
 * name from lsb_release(1).
 *
 * Returns: (transfer none) (type utf8): The name, or %NULL if not known.
 *  The string is owned by @self and must not be freed.
 */
const char *
srt_os_info_get_name (SrtOsInfo *self)
{
  g_return_val_if_fail (SRT_IS_OS_INFO (self), NULL);
  return self->name;
}

/**
 * srt_os_info_get_pretty_name:
 * @self: The OS information
 *
 * Return a human-readable identifier for the operating system,
 * including its version if any, for example `Debian GNU/Linux 10 (buster)`
 * or `Arch Linux`.
 *
 * If the OS uses rolling releases, this will probably be the same as
 * or similar to srt_os_info_get_name().
 *
 * This is the `PRETTY_NAME` in os-release(5). If os-release(5) is not
 * available, future versions of this library might derive a similar
 * name from lsb_release(1).
 *
 * Returns: (transfer none) (type utf8): The name, or %NULL if not known.
 *  The string is owned by @self and must not be freed.
 */
const char *
srt_os_info_get_pretty_name (SrtOsInfo *self)
{
  g_return_val_if_fail (SRT_IS_OS_INFO (self), NULL);
  return self->pretty_name;
}

/**
 * srt_os_info_get_variant:
 * @self: The #SrtSystemInfo object
 *
 * Return a human-readable identifier for the operating system variant,
 * for example `Workstation Edition`, `Server Edition` or
 * `Raspberry Pi Edition`. In operating systems that do not have
 * formal variants this will usually be %NULL.
 *
 * This is the `VARIANT` in os-release(5).
 *
 * Returns: (transfer none) (type utf8): The name, or %NULL if not known.
 *  The string is owned by @self and must not be freed.
 */
const char *
srt_os_info_get_variant (SrtOsInfo *self)
{
  g_return_val_if_fail (SRT_IS_OS_INFO (self), NULL);
  return self->variant;
}

/**
 * srt_os_info_get_variant_id:
 * @self: The #SrtSystemInfo object
 *
 * Return a lower-case machine-readable identifier for the operating system
 * variant in a form suitable for use in filenames, for example
 * `workstation`, `server` or `rpi`. In operating systems that do not
 * have formal variants this will usually be %NULL.
 *
 * This is the `VARIANT_ID` in os-release(5).
 *
 * Returns: (transfer none) (type utf8): The variant ID, or %NULL if not known.
 *  The string is owned by @self and must not be freed.
 */
const char *
srt_os_info_get_variant_id (SrtOsInfo *self)
{
  g_return_val_if_fail (SRT_IS_OS_INFO (self), NULL);
  return self->variant_id;
}

/**
 * srt_os_info_get_version_codename:
 * @self: The #SrtSystemInfo object
 *
 * Return a lower-case machine-readable identifier for the operating
 * system version codename, for example `buster` for Debian 10 "buster".
 * In operating systems that do not use codenames in machine-readable
 * contexts, this will usually be %NULL.
 *
 * This is the `VERSION_CODENAME` in os-release(5). If os-release(5) is not
 * available, future versions of this library might derive a similar
 * codename from lsb_release(1).
 *
 * Returns: (transfer none) (type utf8): The codename, or %NULL if not known.
 *  The string is owned by @self and must not be freed.
 */
const char *
srt_os_info_get_version_codename (SrtOsInfo *self)
{
  g_return_val_if_fail (SRT_IS_OS_INFO (self), NULL);
  return self->version_codename;
}

/**
 * srt_os_info_get_version_id:
 * @self: The #SrtSystemInfo object
 *
 * Return a machine-readable identifier for the operating system version,
 * for example `10` for Debian 10 "buster". In operating systems that
 * only have rolling releases, such as Arch Linux, or in OS branches
 * that behave like rolling releases, such as Debian unstable, this
 * will usually be %NULL.
 *
 * This is the `VERSION_ID` in os-release(5). If os-release(5) is not
 * available, future versions of this library might derive a similar
 * identifier from lsb_release(1).
 *
 * Returns: (transfer none) (type utf8): The ID, or %NULL if not known.
 *  The string is owned by @self and must not be freed.
 */
const char *
srt_os_info_get_version_id (SrtOsInfo *self)
{
  g_return_val_if_fail (SRT_IS_OS_INFO (self), NULL);
  return self->version_id;
}

static const struct
{
  const char *path;
  gboolean only_in_run_host;
}
os_release_paths[] =
{
  { "/etc/os-release", FALSE },
  { "/usr/lib/os-release", FALSE },
  /* https://github.com/flatpak/flatpak/pull/3733 */
  { "/os-release", TRUE }
};

static gboolean
do_line (GHashTable *fields,
         const char *path,
         gchar *line,
         gchar **message_out,
         GError **error)
{
  g_autofree gchar *unquoted = NULL;
  char *equals;

  /* Modify line in-place to strip leading and trailing whitespace */
  g_strstrip (line);

  if (line[0] == '\0' || line[0] == '#')
    return TRUE;

  g_debug ("%s: %s", path, line);

  equals = strchr (line, '=');

  if (equals == NULL)
    return glnx_throw (error, "Unable to parse line \"%s\" in %s: no \"=\" found",
                       line, path);

  unquoted = g_shell_unquote (equals + 1, error);

  if (unquoted == NULL)
    return glnx_prefix_error (error, "Unable to parse line \"%s\" in %s",
                              line, path);

  *equals = '\0';

  if (g_hash_table_contains (fields, line))
    {
      g_autofree gchar *message = NULL;

      message = g_strdup_printf ("%s appears more than once in %s, will use last instance",
                                 line, path);
      g_debug ("%s", message);

      if (message_out != NULL)
        *message_out = g_steal_pointer (&message);
    }

  g_hash_table_replace (fields, g_strdup (line), g_steal_pointer (&unquoted));
  return TRUE;
}

SrtOsInfo *
_srt_os_info_new_from_data (const char *path,
                            const char *contents,
                            gsize len,
                            const char *previous_messages)
{
  g_autoptr(GHashTable) fields = NULL;
  g_autoptr(GString) messages = NULL;
  gsize j;
  gsize beginning_of_line;

  fields = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  messages = g_string_new (previous_messages);

  if (messages->len > 0
      && messages->str[messages->len - 1] != '\n')
    g_string_append_c (messages, '\n');

  beginning_of_line = 0;

  for (j = 0; j < len; j++)
    {
      if (contents[j] == '\n')
        {
          g_autoptr(GError) local_error = NULL;
          g_autofree gchar *line = g_strndup (&contents[beginning_of_line],
                                              j - beginning_of_line);
          g_autofree gchar *message = NULL;

          beginning_of_line = j + 1;

          if (!do_line (fields, path, line, &message, &local_error))
            {
              g_string_append (messages, local_error->message);
              g_string_append_c (messages, '\n');
              g_debug ("%s", local_error->message);
              continue;
            }

          if (message != NULL)
            {
              g_string_append (messages, message);
              g_string_append_c (messages, '\n');
            }
        }
    }

  /* Collect a possible partial line */
  if (beginning_of_line < len)
    {
      g_autofree gchar *line = g_strndup (&contents[beginning_of_line],
                                          len - beginning_of_line);
      g_autofree gchar *message = NULL;
      g_autoptr(GError) local_error = NULL;

      if (!do_line (fields, path, line, &message, &local_error))
        {
          g_string_append (messages, local_error->message);
          g_string_append_c (messages, '\n');
          g_debug ("%s", local_error->message);
        }

      if (message != NULL)
        {
          g_string_append (messages, message);
          g_string_append_c (messages, '\n');
        }
    }

  /* Special case for the Steam Runtime: Flatpak-style scout images have
   * historically not had a VERSION_CODENAME in os-release(5), but
   * we know that version 1 is scout, so let's add it. */
  if (!g_hash_table_contains (fields, "VERSION_CODENAME")
      && g_strcmp0 (g_hash_table_lookup (fields, "ID"), "steamrt") == 0
      && g_strcmp0 (g_hash_table_lookup (fields, "VERSION_ID"), "1") == 0)
    g_hash_table_replace (fields, g_strdup ("VERSION_CODENAME"), g_strdup ("scout"));

  /* Special case for the Steam Runtime: we got this wrong in the past. */
  if (g_strcmp0 (g_hash_table_lookup (fields, "ID_LIKE"), "ubuntu") == 0)
    g_hash_table_replace (fields, g_strdup ("ID_LIKE"), g_strdup ("ubuntu debian"));

  return _srt_os_info_new (fields, messages->str, path);
}

SrtOsInfo *
_srt_os_info_new_from_sysroot (SrtSysroot *sysroot)
{
  g_autoptr(GString) messages = NULL;
  gsize i;

  g_return_val_if_fail (_srt_check_not_setuid (), NULL);
  g_return_val_if_fail (SRT_IS_SYSROOT (sysroot), NULL);

  messages = g_string_new ("");

  for (i = 0; i < G_N_ELEMENTS (os_release_paths); i++)
    {
      g_autoptr(GError) local_error = NULL;
      const char *path = os_release_paths[i].path;
      gboolean only_in_run_host = os_release_paths[i].only_in_run_host;
      g_autofree gchar *contents = NULL;
      gsize len;

      if (only_in_run_host
          && !g_str_has_suffix (sysroot->path, "/run/host"))
        continue;

      if (!_srt_sysroot_load (sysroot, path,
                              SRT_RESOLVE_FLAGS_NONE,
                              NULL, &contents, &len, &local_error))
        {
          if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            {
              g_string_append (messages, local_error->message);
              g_string_append_c (messages, '\n');
            }

          g_debug ("%s", local_error->message);
          continue;
        }

      return _srt_os_info_new_from_data (path, contents, len, messages->str);
    }

  g_string_append (messages, "os-release(5) not found\n");

  return _srt_os_info_new (NULL, messages->str, NULL);
}
