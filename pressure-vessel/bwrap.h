/*
 * Copyright © 2017-2019 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib.h>
#include <glib-object.h>

#include "libglnx.h"

#include "flatpak-bwrap-private.h"
#include "flatpak-exports-private.h"
#include "steam-runtime-tools/bwrap-internal.h"
#include "steam-runtime-tools/env-overlay-internal.h"
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/resolve-in-sysroot-internal.h"
#include "steam-runtime-tools/steam-internal.h"
#include "utils.h"

gboolean pv_bwrap_run_sync (FlatpakBwrap *bwrap,
                            int *exit_status_out,
                            GError **error);
gboolean pv_bwrap_execve (FlatpakBwrap *bwrap,
                          int *inherit_fds,
                          size_t n_inherit_fds,
                          GError **error);
gboolean pv_bwrap_bind_usr (FlatpakBwrap *bwrap,
                            const char *provider_in_host_namespace,
                            int provider_fd,
                            const char *provider_in_container_namespace,
                            GError **error);
void pv_bwrap_copy_tree (FlatpakBwrap *bwrap,
                         const char *source,
                         const char *dest);
void pv_bwrap_add_api_filesystems (FlatpakBwrap *bwrap,
                                   FlatpakFilesystemMode sysfs_mode,
                                   SrtSteamCompatFlags compat_flags);

static inline gboolean
pv_bwrap_was_finished (FlatpakBwrap *bwrap)
{
  g_return_val_if_fail (bwrap != NULL, FALSE);

  return (bwrap->argv->len >= 1 &&
          bwrap->argv->pdata[bwrap->argv->len - 1] == NULL);
}

FlatpakBwrap *pv_bwrap_copy (FlatpakBwrap *bwrap);

GStrv pv_bwrap_steal_envp (FlatpakBwrap *bwrap);

gboolean pv_bwrap_append_adjusted_exports (FlatpakBwrap *to,
                                           FlatpakBwrap *from,
                                           const char *home,
                                           SrtSysroot *interpreter_root,
                                           PvWorkaroundFlags workarounds,
                                           GError **error);

void pv_bwrap_container_env_to_subsandbox_argv (FlatpakBwrap *flatpak_subsandbox,
                                                SrtEnvOverlay *container_env);
void pv_bwrap_container_env_to_envp (FlatpakBwrap *bwrap,
                                     SrtEnvOverlay *container_env);
void pv_bwrap_filtered_container_env_to_bwrap_argv (FlatpakBwrap *bwrap,
                                                    SrtEnvOverlay *container_env);
