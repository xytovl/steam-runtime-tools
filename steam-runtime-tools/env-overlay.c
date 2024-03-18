/*
 * Copyright © 2014-2018 Red Hat, Inc
 * Copyright © 2020-2021 Collabora Ltd.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "steam-runtime-tools/env-overlay-internal.h"

#include "steam-runtime-tools/utils-internal.h"

#include <fnmatch.h>

SrtEnvOverlay *
_srt_env_overlay_new (void)
{
  g_autoptr(SrtEnvOverlay) self = g_slice_new0 (SrtEnvOverlay);

  self->values = g_hash_table_new_full (g_str_hash, g_str_equal,
                                        g_free, g_free);
  return g_steal_pointer (&self);
}

void
_srt_env_overlay_free (SrtEnvOverlay *self)
{
  g_return_if_fail (self != NULL);

  g_clear_pointer (&self->values, g_hash_table_unref);
  g_slice_free (SrtEnvOverlay, self);
}

/*
 * _srt_env_overlay_set:
 * @self: Environment variables to set or unset
 * @var: (type filename): An environment variable name
 * @val: (type filename): A value for the environment variable
 *
 * Set @var to @val, which may be %NULL to unset it.
 */
void
_srt_env_overlay_set (SrtEnvOverlay *self,
                      const char *var,
                      const char *val)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (var != NULL);
  /* val may be NULL, to unset it */

  _srt_env_overlay_take (self, g_strdup (var), g_strdup (val));
}

/*
 * _srt_env_overlay_set_cli:
 * @self: Environment variables to set or unset
 * @option_name: An option name
 * @value: Value associated with @option_name
 * @error: Used to raise a %G_OPTION_ERROR on error
 *
 * If @value is a valid environment variable setting in the format
 * `NAME=VALUE`, set *NAME* to *VALUE* as if via _srt_env_overlay_set().
 *
 * Returns: %TRUE on success
 */
gboolean
_srt_env_overlay_set_cli (SrtEnvOverlay *self,
                          const char *option_name,
                          const char *value,
                          GError **error)
{
  g_auto(GStrv) split = NULL;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (option_name != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  split = g_strsplit (value, "=", 2);

  if (split == NULL ||
      split[0] == NULL ||
      split[0][0] == 0 ||
      split[1] == NULL)
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                   "Invalid environment variable format \"%s\" for %s, should be NAME=VALUE",
                   value, option_name);
      return FALSE;
    }

  _srt_env_overlay_take (self,
                         g_steal_pointer (&split[0]),
                         g_steal_pointer (&split[1]));
  return TRUE;
}

/*
 * _srt_env_overlay_unset_cli:
 * @self: Environment variables to set or unset
 * @option_name: An option name
 * @value: Value associated with @option_name
 * @error: Used to raise a %G_OPTION_ERROR on error
 *
 * If @value is a valid environment variable name, mark it to be unset,
 * as if via _srt_env_overlay_set() with @val set to %NULL.
 *
 * Returns: %TRUE on success
 */
gboolean
_srt_env_overlay_unset_cli (SrtEnvOverlay *self,
                            const char *option_name,
                            const char *value,
                            GError **error)
{
  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (option_name != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (value == NULL || value[0] == '\0')
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                   "Environment variable name for %s must be non-empty",
                   option_name);
      return FALSE;
    }

  _srt_env_overlay_set (self, value, NULL);
  return TRUE;
}

/*
 * _srt_env_overlay_take:
 * @self: Environment variables to set or unset
 * @var: (type filename) (transfer full): An environment variable
 * @val: (type filename) (transfer full): A value for the environment variable
 *
 * Set @var to @val, which may be %NULL to unset it.
 * Ownership of the two strings is taken.
 */
void
_srt_env_overlay_take (SrtEnvOverlay *self,
                       gchar *var,
                       gchar *val)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (var != NULL);
  /* val may be NULL, to unset it */

  g_hash_table_replace (self->values, var, val);
}

/*
 * _srt_env_overlay_inherit:
 * @self: Environment variables to set or unset
 * @var: (type filename): An environment variable name
 *
 * Set @var to whatever value it happens to have inherited.
 */
void
_srt_env_overlay_inherit (SrtEnvOverlay *self,
                          const char *var)
{
  g_return_if_fail (self != NULL);
  g_return_if_fail (var != NULL);

  g_hash_table_remove (self->values, var);
}

/*
 * _srt_env_overlay_inherit_cli:
 * @self: Environment variables to set or unset
 * @option_name: An option name
 * @value: Value associated with @option_name
 * @error: Used to raise a %G_OPTION_ERROR on error
 *
 * If @value is a valid environment variable name, set it to be inherited
 * as if via _srt_env_overlay_inherit().
 *
 * Returns: %TRUE on success
 */
gboolean
_srt_env_overlay_inherit_cli (SrtEnvOverlay *self,
                              const char *option_name,
                              const char *value,
                              GError **error)
{
  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (option_name != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (value == NULL || value[0] == '\0')
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                   "Environment variable name for %s must be non-empty",
                   option_name);
      return FALSE;
    }

  _srt_env_overlay_inherit (self, value);
  return TRUE;
}

/*
 * _srt_env_overlay_get_vars:
 * @self: Environment variables to set or unset
 *
 * Return a sorted list of environment variables that are to be overridden.
 *
 * Returns: (transfer container): The variables that are set or forced
 *  to be unset, but not the variables that are inherited
 */
GList *
_srt_env_overlay_get_vars (SrtEnvOverlay *self)
{
  g_return_val_if_fail (self != NULL, NULL);

  return g_list_sort (g_hash_table_get_keys (self->values),
                      _srt_generic_strcmp0);
}

/*
 * _srt_env_overlay_get:
 * @self: Environment variables to set or unset
 * @var: (type filename): An environment variable name
 *
 * Get the value of an overridden environment variable.
 *
 * Returns: (nullable): The value of @var, or %NULL if @var is either
 *  unset or inherited
 */
const char *
_srt_env_overlay_get (SrtEnvOverlay *self,
                      const char *var)
{
  g_return_val_if_fail (self != NULL, NULL);

  return g_hash_table_lookup (self->values, var);
}

/*
 * _srt_env_overlay_inherit_matching_pattern:
 * @self: Environment variables to set or unset
 * @pattern: (type filename): A glob pattern for environment variable names
 *
 * For each environment variable that is either set or unset in @self,
 * if the name matches @pattern, mark it to be inherited as if via
 * _srt_env_overlay_inherit().
 */
void
_srt_env_overlay_inherit_matching_pattern (SrtEnvOverlay *self,
                                           const char *pattern)
{
  GHashTableIter iter;
  gpointer k;

  g_return_if_fail (self != NULL);
  g_return_if_fail (pattern != NULL);

  g_hash_table_iter_init (&iter, self->values);

  while (g_hash_table_iter_next (&iter, &k, NULL))
    {
      if (fnmatch (pattern, k, 0) == 0)
        g_hash_table_iter_remove (&iter);
    }
}

/*
 * _srt_env_overlay_inherit_matching_pattern_cli:
 * @self: Environment variables to set or unset
 * @option_name: An option name
 * @value: Value associated with @option_name
 * @error: Used to raise a %G_OPTION_ERROR on error
 *
 * Set all environment variables matching the glob-style pattern @value
 * to be inherited as if via _srt_env_overlay_inherit().
 *
 * Returns: %TRUE on success
 */
gboolean
_srt_env_overlay_inherit_matching_pattern_cli (SrtEnvOverlay *self,
                                               const char *option_name,
                                               const char *value,
                                               GError **error)
{
  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (option_name != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  _srt_env_overlay_inherit_matching_pattern (self, value);
  return TRUE;
}

/*
 * _srt_env_overlay_pass_cli:
 * @self: Environment variables to set or unset
 * @option_name: An option name
 * @value: Value associated with @option_name
 * @envp: Environment block used to look up @value
 * @error: Used to raise a %G_OPTION_ERROR on error
 *
 * Set the environment variable @value to whatever its value is in @envp,
 * or mark it to be unset if it is not in @envp.
 *
 * Returns: %TRUE on success
 */
gboolean
_srt_env_overlay_pass_cli (SrtEnvOverlay *self,
                           const char *option_name,
                           const char *value,
                           const char * const *envp,
                           GError **error)
{
  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (option_name != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  _srt_env_overlay_set (self, value, _srt_environ_getenv (envp, value));
  return TRUE;
}

/*
 * _srt_env_overlay_pass_matching_pattern_cli:
 * @self: Environment variables to set or unset
 * @option_name: An option name
 * @value: Value associated with @option_name
 * @envp: Environment block used to look up @value
 * @error: Used to raise a %G_OPTION_ERROR on error
 *
 * Set all environment variables matching the glob-style pattern @value
 * to be inherited as if via _srt_env_overlay_inherit().
 *
 * Returns: %TRUE on success
 */
gboolean
_srt_env_overlay_pass_matching_pattern_cli (SrtEnvOverlay *self,
                                            const char *option_name,
                                            const char *value,
                                            const char * const *envp,
                                            GError **error)
{
  const char * const *iter;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (option_name != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (envp == NULL)
    return TRUE;

  for (iter = envp; *iter != NULL; iter++)
    {
      g_auto(GStrv) split = g_strsplit (*iter, "=", 2);

      if (split == NULL ||
          split[0] == NULL ||
          split[0][0] == 0 ||
          split[1] == NULL)
        continue;

      if (fnmatch (value, split[0], 0) == 0)
        _srt_env_overlay_take (self,
                               g_steal_pointer (&split[0]),
                               g_steal_pointer (&split[1]));
    }

  return TRUE;
}

gboolean
_srt_env_overlay_env_fd_cli (SrtEnvOverlay *self,
                             const char *option_name,
                             const char *value,
                             GError **error)
{
  g_autofree gchar *proc_filename = NULL;
  g_autofree gchar *env_block = NULL;
  gsize remaining;
  const char *p;
  guint64 fd;
  gchar *endptr;

  g_assert (self != NULL);

  fd = g_ascii_strtoull (value, &endptr, 10);

  if (endptr == NULL || *endptr != '\0' || fd > G_MAXINT)
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                   "Not a valid file descriptor: %s", value);
      return FALSE;
    }

  proc_filename = g_strdup_printf ("/proc/self/fd/%d", (int) fd);

  if (!g_file_get_contents (proc_filename, &env_block, &remaining, error))
    return FALSE;

  p = env_block;

  while (remaining > 0)
    {
      g_autofree gchar *var = NULL;
      g_autofree gchar *val = NULL;
      size_t len = strnlen (p, remaining);
      const char *equals;

      g_assert (len <= remaining);

      equals = memchr (p, '=', len);

      if (equals == NULL || equals == p)
        {
          g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                       "Environment variable must be given in the form VARIABLE=VALUE, not %.*s",
                       (int) len, p);
          return FALSE;
        }

      var = g_strndup (p, equals - p);
      val = g_strndup (equals + 1, len - (equals - p) - 1);
      _srt_env_overlay_take (self,
                             g_steal_pointer (&var),
                             g_steal_pointer (&val));

      p += len;
      remaining -= len;

      if (remaining > 0)
        {
          g_assert (*p == '\0');
          p += 1;
          remaining -= 1;
        }
    }

  if (fd >= 3)
    close (fd);

  return TRUE;
}
