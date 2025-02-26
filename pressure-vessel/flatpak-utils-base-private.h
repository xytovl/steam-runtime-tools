/*
 * Taken from Flatpak
 * Last updated: Flatpak 1.15.10
 * Copyright © 2019 Red Hat, Inc
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

#ifndef __FLATPAK_UTILS_BASE_H__
#define __FLATPAK_UTILS_BASE_H__

#include <glib.h>
#include <gio/gio.h>

#ifndef G_DBUS_METHOD_INVOCATION_HANDLED
# define G_DBUS_METHOD_INVOCATION_HANDLED TRUE
# define G_DBUS_METHOD_INVOCATION_UNHANDLED FALSE
#endif

const char * flatpak_get_tzdir (void);

char * flatpak_get_timezone (void);

char * flatpak_readlink (const char *path,
                         GError    **error);
char * flatpak_resolve_link (const char *path,
                             GError    **error);
char * flatpak_canonicalize_filename (const char *path);

#endif /* __FLATPAK_UTILS_BASE_H__ */
