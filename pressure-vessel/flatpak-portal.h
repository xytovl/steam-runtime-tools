/*
 * Taken from Flatpak
 * Last updated: Flatpak 1.15.10
 *
 * Copyright © 2018 Red Hat, Inc
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

#ifndef __FLATPAK_PORTAL_H__
#define __FLATPAK_PORTAL_H__

#define FLATPAK_PORTAL_BUS_NAME "org.freedesktop.portal.Flatpak"
#define FLATPAK_PORTAL_PATH "/org/freedesktop/portal/Flatpak"
#define FLATPAK_PORTAL_INTERFACE FLATPAK_PORTAL_BUS_NAME
#define FLATPAK_PORTAL_INTERFACE_UPDATE_MONITOR FLATPAK_PORTAL_BUS_NAME ".UpdateMonitor"

typedef enum {
  FLATPAK_SPAWN_FLAGS_CLEAR_ENV = 1 << 0,
  FLATPAK_SPAWN_FLAGS_LATEST_VERSION = 1 << 1,
  FLATPAK_SPAWN_FLAGS_SANDBOX = 1 << 2,
  FLATPAK_SPAWN_FLAGS_NO_NETWORK = 1 << 3,
  FLATPAK_SPAWN_FLAGS_WATCH_BUS = 1 << 4,
  FLATPAK_SPAWN_FLAGS_EXPOSE_PIDS = 1 << 5,
  FLATPAK_SPAWN_FLAGS_NOTIFY_START = 1 << 6,
  FLATPAK_SPAWN_FLAGS_SHARE_PIDS = 1 << 7,
  FLATPAK_SPAWN_FLAGS_EMPTY_APP = 1 << 8,
  FLATPAK_SPAWN_FLAGS_NONE = 0
} FlatpakSpawnFlags;

typedef enum {
  FLATPAK_SPAWN_SANDBOX_FLAGS_SHARE_DISPLAY = 1 << 0,
  FLATPAK_SPAWN_SANDBOX_FLAGS_SHARE_SOUND = 1 << 1,
  FLATPAK_SPAWN_SANDBOX_FLAGS_SHARE_GPU = 1 << 2,
  FLATPAK_SPAWN_SANDBOX_FLAGS_ALLOW_DBUS = 1 << 3,
  FLATPAK_SPAWN_SANDBOX_FLAGS_ALLOW_A11Y = 1 << 4,
  FLATPAK_SPAWN_SANDBOX_FLAGS_NONE = 0
} FlatpakSpawnSandboxFlags;


typedef enum {
  FLATPAK_SPAWN_SUPPORT_FLAGS_EXPOSE_PIDS = 1 << 0,
  FLATPAK_SPAWN_SUPPORT_FLAGS_NONE = 0
} FlatpakSpawnSupportFlags;

/* The same flag is reused: this feature is available under the same
 * circumstances */
#define FLATPAK_SPAWN_SUPPORT_FLAGS_SHARE_PIDS FLATPAK_SPAWN_SUPPORT_FLAGS_EXPOSE_PIDS

#define FLATPAK_SPAWN_FLAGS_ALL (FLATPAK_SPAWN_FLAGS_CLEAR_ENV | \
                                 FLATPAK_SPAWN_FLAGS_LATEST_VERSION | \
                                 FLATPAK_SPAWN_FLAGS_SANDBOX | \
                                 FLATPAK_SPAWN_FLAGS_NO_NETWORK | \
                                 FLATPAK_SPAWN_FLAGS_WATCH_BUS | \
                                 FLATPAK_SPAWN_FLAGS_EXPOSE_PIDS | \
                                 FLATPAK_SPAWN_FLAGS_NOTIFY_START | \
                                 FLATPAK_SPAWN_FLAGS_SHARE_PIDS | \
                                 FLATPAK_SPAWN_FLAGS_EMPTY_APP)

#define FLATPAK_SPAWN_SANDBOX_FLAGS_ALL (FLATPAK_SPAWN_SANDBOX_FLAGS_SHARE_DISPLAY | \
                                         FLATPAK_SPAWN_SANDBOX_FLAGS_SHARE_SOUND | \
                                         FLATPAK_SPAWN_SANDBOX_FLAGS_SHARE_GPU | \
                                         FLATPAK_SPAWN_SANDBOX_FLAGS_ALLOW_DBUS | \
                                         FLATPAK_SPAWN_SANDBOX_FLAGS_ALLOW_A11Y)

#endif /* __FLATPAK_PORTAL_H__ */
