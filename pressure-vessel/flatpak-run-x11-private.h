/*
 * Taken from Flatpak
 * Last updated: Flatpak 1.15.10
 * Copyright © 2014 Red Hat, Inc
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#pragma once

#include "libglnx.h"

#include "flatpak-bwrap-private.h"
#include "flatpak-common-types-private.h"
#include "flatpak-context-private.h"

G_BEGIN_DECLS

void flatpak_run_add_x11_args (FlatpakBwrap         *bwrap,
                               gboolean              allowed,
                               FlatpakContextShares  shares);

gboolean flatpak_run_parse_x11_display (const char  *display,
                                        int         *family,
                                        char       **x11_socket,
                                        char       **remote_host,
                                        char       **original_display_nr,
                                        GError     **error);

G_END_DECLS
