/*
 * Contains code taken from Flatpak.
 *
 * Copyright © 2014-2019 Red Hat, Inc
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

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>

#include "steam-runtime-tools/bwrap-internal.h"
#include "steam-runtime-tools/missing-internal.h"

typedef enum
{
  /* Old (presumably setuid) system copy of bwrap < 0.5.0 with no --perms */
  PV_WORKAROUND_FLAGS_BWRAP_NO_PERMS = (1 << 0),
  /* https://github.com/canonical/steam-snap/issues/356 */
  PV_WORKAROUND_FLAGS_STEAMSNAP_356 = (1 << 1),
  /* https://github.com/canonical/steam-snap/issues/369 */
  PV_WORKAROUND_FLAGS_STEAMSNAP_369 = (1 << 2),
  /* https://github.com/canonical/steam-snap/issues/370 */
  PV_WORKAROUND_FLAGS_STEAMSNAP_370 = (1 << 3),
  /* https://github.com/canonical/steam-snap/issues/359 */
  PV_WORKAROUND_FLAGS_STEAMSNAP_359 = (1 << 4),
  PV_WORKAROUND_FLAGS_NONE = 0
} PvWorkaroundFlags;

#define PV_WORKAROUND_FLAGS_SNAP \
  (PV_WORKAROUND_FLAGS_STEAMSNAP_356 \
   | PV_WORKAROUND_FLAGS_STEAMSNAP_359 \
   | PV_WORKAROUND_FLAGS_STEAMSNAP_369 \
   | PV_WORKAROUND_FLAGS_STEAMSNAP_370)

#define PV_WORKAROUND_FLAGS_ALL \
  (PV_WORKAROUND_FLAGS_BWRAP_NO_PERMS \
   | PV_WORKAROUND_FLAGS_SNAP)

PvWorkaroundFlags pv_get_workarounds (SrtBwrapFlags bwrap_flags,
                                      const char * const *envp);

void pv_search_path_append (GString *search_path,
                            const gchar *item);

gboolean pv_run_sync (const char * const * argv,
                      const char * const * envp,
                      int *exit_status_out,
                      char **output_out,
                      GError **error);

gpointer pv_hash_table_get_first_key (GHashTable *table,
                                      GCompareFunc cmp);

gchar *pv_current_namespace_path_to_host_path (const gchar *current_env_path);

void pv_delete_dangling_symlink (int dirfd,
                                 const char *debug_path,
                                 const char *name);

int pv_count_decimal_digits (gsize n);

gchar *pv_generate_unique_filepath (const gchar *sub_dir,
                                    int digits,
                                    gsize seq,
                                    const gchar *file,
                                    const gchar *multiarch_tuple,
                                    GHashTable *files_set);

gchar *pv_stat_describe_permissions (const struct stat *stat_buf);
