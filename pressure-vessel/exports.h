/*
 * Copyright © 2017-2020 Collabora Ltd.
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

#include "libglnx.h"

#include "flatpak-exports-private.h"
#include "steam-runtime-tools/glib-backports-internal.h"

void pv_export_symlink_targets (FlatpakExports *exports,
                                const char *source,
                                const char *log_as);
void pv_exports_expose_or_log (FlatpakExports *exports,
                               FlatpakFilesystemMode mode,
                               const char *path);
void pv_exports_expose_or_warn (FlatpakExports *exports,
                                FlatpakFilesystemMode mode,
                                const char *path);
void pv_exports_expose_quietly (FlatpakExports *exports,
                                FlatpakFilesystemMode mode,
                                const char *path);
void pv_exports_mask_or_log (FlatpakExports *exports,
                             const char *path);
void pv_exports_ensure_dir_or_warn (FlatpakExports *exports,
                                    const char *path);
