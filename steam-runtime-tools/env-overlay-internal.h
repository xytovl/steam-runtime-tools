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

SrtEnvOverlay *_srt_env_overlay_new (void);
void _srt_env_overlay_free (SrtEnvOverlay *self);

void _srt_env_overlay_set (SrtEnvOverlay *self,
                           const char *var,
                           const char *val);
void _srt_env_overlay_inherit (SrtEnvOverlay *self,
                               const char *var);

GList *_srt_env_overlay_get_vars (SrtEnvOverlay *self);
const char *_srt_env_overlay_get (SrtEnvOverlay *self,
                                  const char *var);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtEnvOverlay, _srt_env_overlay_free)
