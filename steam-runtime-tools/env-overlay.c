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

  self->refcount = 1;
  self->values = g_hash_table_new_full (g_str_hash, g_str_equal,
                                        g_free, g_free);
  return g_steal_pointer (&self);
}

SrtEnvOverlay *
_srt_env_overlay_ref (SrtEnvOverlay *self)
{
  g_return_val_if_fail (self->refcount > 0, NULL);
  self->refcount++;
  return self;
}

void
_srt_env_overlay_unref (void *p)
{
  SrtEnvOverlay *self = p;

  g_return_if_fail (self != NULL);
  g_return_if_fail (self->refcount > 0);

  if (--self->refcount > 0)
    return;

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
static gboolean
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
static gboolean
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
static gboolean
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
 * _srt_env_overlay_contains:
 * @self: Environment variables to set or unset
 * @var: (type filename): An environment variable name
 *
 * Returns: %TRUE if @var is either set or forced to be unset,
 *  or %FALSE if it is inherited
 */
gboolean
_srt_env_overlay_contains (SrtEnvOverlay *self,
                           const char *var)
{
  g_return_val_if_fail (self != NULL, FALSE);

  return g_hash_table_lookup_extended (self->values, var, NULL, NULL);
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
static gboolean
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

static gboolean
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

/*
 * _srt_env_overlay_apply:
 * @self: Variables to set and unset
 * @envp: (transfer full): An environment block
 *
 * Return a version of @envp that has been modified to set and unset
 * its environment variables according to the instructions in @self.
 *
 * Returns: (transfer full): The new environment block
 */
GStrv
_srt_env_overlay_apply (SrtEnvOverlay *self,
                        GStrv envp)
{
  g_autoptr(GList) vars = NULL;
  const GList *iter;

  g_return_val_if_fail (self != NULL, envp);

  vars = _srt_env_overlay_get_vars (self);

  for (iter = vars; iter != NULL; iter = iter->next)
    {
      const char *value = g_hash_table_lookup (self->values, iter->data);

      if (value != NULL)
        envp = g_environ_setenv (envp, iter->data, value, TRUE);
      else
        envp = g_environ_unsetenv (envp, iter->data);
    }

  return g_steal_pointer (&envp);
}

static void
byte_array_append_env0 (GByteArray *arr,
                        const char *var,
                        const char *val)
{
  g_byte_array_append (arr, (const guint8 *) var, strlen (var));
  g_byte_array_append (arr, (const guint8 *) "=", 1);
  /* This appends the value plus \0 */
  g_byte_array_append (arr, (const guint8 *) val, strlen (val) + 1);
}

/*
 * _srt_env_overlay_to_env0:
 * @self: Variables to set and unset
 *
 * Return all of the variables set by @self, as a buffer in `env -0` format.
 * Unset and inherited variables are ignored.
 *
 * Returns: (transfer full): `NAME=VALUE\0NAME=VALUE\0...`
 */
GBytes *
_srt_env_overlay_to_env0 (SrtEnvOverlay *self)
{
  GByteArray *arr = NULL;
  g_autoptr(GList) vars = NULL;
  const GList *iter;

  g_return_val_if_fail (self != NULL, NULL);

  arr = g_byte_array_new ();
  vars = _srt_env_overlay_get_vars (self);

  for (iter = vars; iter != NULL; iter = iter->next)
    {
      const char *value = g_hash_table_lookup (self->values, iter->data);

      if (value != NULL)
        byte_array_append_env0 (arr, iter->data, value);
    }

  return g_byte_array_free_to_bytes (arr);
}

/*
 * _srt_env_overlay_to_shell:
 * @self: Variables to set and unset
 *
 * Return all of the variables set by @self, as a buffer in a format
 * that could be evaluated by a POSIX shell using `sh -c` or the
 * `eval` builtin.
 *
 * Variables that are set to a value are output in the form
 * `export NAME=VALUE` followed by a newline, with *VALUE* quoted if required.
 *
 * Variables that are unset are output in the form `unset NAME` followed
 * by a newline.
 *
 * Variables whose names would not be acceptable in shell syntax are ignored.
 *
 * Inherited variables are ignored.
 *
 * Returns: (transfer full): a shell script
 */
gchar *
_srt_env_overlay_to_shell (SrtEnvOverlay *self)
{
  GString *buf = NULL;
  g_autoptr(GList) vars = NULL;
  const GList *iter;

  g_return_val_if_fail (self != NULL, NULL);

  buf = g_string_new ("");
  vars = _srt_env_overlay_get_vars (self);

  for (iter = vars; iter != NULL; iter = iter->next)
    {
      const char *name = iter->data;
      const char *value = g_hash_table_lookup (self->values, name);

      if (!_srt_is_identifier (name))
        {
          g_debug ("Cannot export variable \"%s\": not a valid shell variable",
                   name);
          continue;
        }

      if (value != NULL)
        {
          g_autofree gchar *escaped = g_shell_quote (value);

          g_string_append_printf (buf, "export %s=%s\n", name, escaped);
        }
      else
        {
          g_string_append_printf (buf, "unset %s\n", name);
        }
    }

  return g_string_free (buf, FALSE);
}

static gboolean
opt_env_cb (const char *option_name,
            const gchar *value,
            gpointer data,
            GError **error)
{
  return _srt_env_overlay_set_cli (data, option_name, value, error);
}

static gboolean
opt_env_fd_cb (const char *option_name,
               const gchar *value,
               gpointer data,
               GError **error)
{
  return _srt_env_overlay_env_fd_cli (data, option_name, value, error);
}

static gboolean
opt_inherit_env_cb (const char *option_name,
                    const gchar *value,
                    gpointer data,
                    GError **error)
{
  return _srt_env_overlay_inherit_cli (data, option_name, value, error);
}

static gboolean
opt_inherit_env_matching_cb (const char *option_name,
                             const gchar *value,
                             gpointer data,
                             GError **error)
{
  return _srt_env_overlay_inherit_matching_pattern_cli (data, option_name,
                                                        value, error);
}

static gboolean
opt_unset_env_cb (const char *option_name,
                  const gchar *value,
                  gpointer data,
                  GError **error)
{
  return _srt_env_overlay_unset_cli (data, option_name, value, error);
}

static const GOptionEntry options[] =
{
  { "env", '\0',
    G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, opt_env_cb,
    "Set environment variable.", "VAR=VALUE" },
  { "env-fd", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, opt_env_fd_cb,
    "Read environment variables in env -0 format from FD", "FD" },
  { "inherit-env", '\0',
    G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, opt_inherit_env_cb,
    "Undo a previous --env, --unset-env, --pass-env, etc.", "VAR" },
  { "inherit-env-matching", '\0',
    G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, opt_inherit_env_matching_cb,
    "Undo previous --env, --unset-env, etc. matching a shell-style wildcard",
    "WILDCARD" },
  { "unset-env", '\0',
    G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, opt_unset_env_cb,
    "Unset environment variable, like env -u.", "VAR" },
  { NULL }
};

/*
 * Returns: (transfer full): a #GOptionGroup
 */
GOptionGroup *
_srt_env_overlay_create_option_group (SrtEnvOverlay *self)
{
  GOptionGroup *group = NULL;

  group = g_option_group_new ("environment",
                              "Environment Options:",
                              "Environment options",
                              _srt_env_overlay_ref (self),
                              _srt_env_overlay_unref);
  g_option_group_add_entries (group, options);

  return g_steal_pointer (&group);
}
