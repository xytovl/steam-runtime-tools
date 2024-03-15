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

/*
 * SrtEnvOverlay:
 *
 * A set of environment variables, each in one of these states:
 *
 * - Set to a value (empty or non-empty)
 * - Forced to be unset
 * - Inherited from some execution environment that is unknown to us
 *
 * We represent this as follows:
 *
 * - Set to a value: values[VAR] = VAL
 * - Forced to be unset: values[VAR] = NULL
 * - Inherited from the execution environment: VAR not in `values`
 */
struct _SrtEnvOverlay
{
  /* (element-type filename filename) */
  GHashTable *values;
};

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

  g_hash_table_replace (self->values, g_strdup (var), g_strdup (val));
}

/*
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
