/*
 * Copyright © 2014-2019 Red Hat, Inc
 * Copyright © 2017-2021 Collabora Ltd.
 *
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

#include <glib.h>

#include "steam-runtime-tools/bwrap-internal.h"
#include "steam-runtime-tools/env-overlay-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include "bwrap.h"
#include "flatpak-bwrap-private.h"
#include "flatpak-exports-private.h"
#include "runtime.h"
#include "wrap-context.h"
#include "wrap-discord.h"
#include "wrap-home.h"
#include "wrap-openxr.h"
#include "wrap-pipewire.h"

gchar *pv_wrap_check_bwrap (gboolean only_prepare,
                            SrtBwrapFlags *flags_out,
                            GError **error);

FlatpakBwrap *pv_wrap_share_sockets (SrtEnvOverlay *container_env,
                                     const char * const *original_environ,
                                     gboolean using_a_runtime,
                                     gboolean is_flatpak_env);

void pv_wrap_set_icons_env_vars (SrtEnvOverlay *container_env,
                                 const char * const *original_environ);

gboolean pv_wrap_use_host_os (int root_fd,
                              FlatpakExports *exports,
                              FlatpakBwrap *bwrap,
                              SrtDirentCompareFunc arbitrary_dirent_order,
                              GError **error);

gboolean pv_export_root_dirs_like_filesystem_host (int root_fd,
                                                   FlatpakExports *exports,
                                                   FlatpakFilesystemMode mode,
                                                   SrtDirentCompareFunc arbitrary_dirent_order,
                                                   GError **error);

void pv_wrap_move_into_scope (const char *steam_app_id);

/**
 * PvAppendPreloadFlags:
 * @PV_APPEND_PRELOAD_FLAGS_FLATPAK_SUBSANDBOX: The game will be run in
 *  a Flatpak subsandbox
 * @PV_APPEND_PRELOAD_FLAGS_REMOVE_GAME_OVERLAY: Disable the Steam Overlay
 * @PV_APPEND_PRELOAD_FLAGS_NONE: None of the above
 *
 * Flags affecting the behaviour of pv_wrap_append_preload().
 */
typedef enum
{
  PV_APPEND_PRELOAD_FLAGS_FLATPAK_SUBSANDBOX = (1 << 0),
  PV_APPEND_PRELOAD_FLAGS_REMOVE_GAME_OVERLAY = (1 << 1),
  PV_APPEND_PRELOAD_FLAGS_IN_UNIT_TESTS = (1 << 2),
  PV_APPEND_PRELOAD_FLAGS_NONE = 0
} PvAppendPreloadFlags;

void pv_wrap_append_preload (GPtrArray *argv,
                             const char *variable,
                             const char *option,
                             const char *preload,
                             GStrv env,
                             PvAppendPreloadFlags flags,
                             PvRuntime *runtime,
                             FlatpakExports *exports);

gboolean pv_wrap_maybe_load_nvidia_modules (GError **error);

void pv_wrap_detect_virtualization (SrtSysroot **interpreter_root_out,
                                    SrtMachineType *host_machine_out);

void pv_share_temp_dir (FlatpakExports *exports,
                        SrtEnvOverlay *container_env);
void pv_bind_and_propagate_from_environ (PvWrapContext *self,
                                         SrtSysroot *sysroot,
                                         PvHomeMode home_mode,
                                         FlatpakExports *exports,
                                         SrtEnvOverlay *container_env);
