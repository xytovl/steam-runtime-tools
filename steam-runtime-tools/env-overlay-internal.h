/*<private_header>*/
/*
 * Copyright © 2014-2018 Red Hat, Inc
 * Copyright © 2020 Collabora Ltd.
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

#pragma once

#include "libglnx.h"

#include "steam-runtime-tools/glib-backports-internal.h"

typedef struct _SrtEnvOverlay SrtEnvOverlay;

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
  size_t refcount;

  /* (element-type filename filename) */
  GHashTable *values;
};

SrtEnvOverlay *_srt_env_overlay_new (void);
SrtEnvOverlay *_srt_env_overlay_ref (SrtEnvOverlay *self);
void _srt_env_overlay_unref (void *p);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtEnvOverlay, _srt_env_overlay_unref)

void _srt_env_overlay_set (SrtEnvOverlay *self,
                           const char *var,
                           const char *val);
void _srt_env_overlay_take (SrtEnvOverlay *self,
                            gchar *var,
                            gchar *val);

void _srt_env_overlay_inherit (SrtEnvOverlay *self,
                               const char *var);

void _srt_env_overlay_inherit_matching_pattern (SrtEnvOverlay *self,
                                                const char *pattern);

gboolean _srt_env_overlay_pass_cli (SrtEnvOverlay *self,
                                    const char *option_name,
                                    const char *value,
                                    const char * const *envp,
                                    GError **error);
gboolean _srt_env_overlay_pass_matching_pattern_cli (SrtEnvOverlay *self,
                                                     const char *option_name,
                                                     const char *value,
                                                     const char * const *envp,
                                                     GError **error);

GList *_srt_env_overlay_get_vars (SrtEnvOverlay *self);
gboolean _srt_env_overlay_contains (SrtEnvOverlay *self,
                                    const char *var);
const char *_srt_env_overlay_get (SrtEnvOverlay *self,
                                  const char *var);

GStrv _srt_env_overlay_apply (SrtEnvOverlay *self,
                              GStrv envp);
GBytes *_srt_env_overlay_to_env0 (SrtEnvOverlay *self);

GOptionGroup *_srt_env_overlay_create_option_group (SrtEnvOverlay *self);
