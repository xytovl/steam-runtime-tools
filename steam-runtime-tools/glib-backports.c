/*
 * Backports from GLib
 *
 *  Copyright 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 *  Copyright 1997-2000 GLib team
 *  Copyright 2000-2016 Red Hat, Inc.
 *  Copyright 2013-2022 Collabora Ltd.
 *  Copyright 2017-2022 Endless OS Foundation, LLC
 *  Copyright 2018 Georges Basile Stavracas Neto
 *  Copyright 2018 Philip Withnall
 *  Copyright 2018 Will Thompson
 *  Copyright 2021 Joshua Lee
 *  g_execvpe implementation based on GNU libc execvp:
 *   Copyright 1991, 92, 95, 96, 97, 98, 99 Free Software Foundation, Inc.
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

#include "steam-runtime-tools/glib-backports-internal.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <glib-object.h>

#if !GLIB_CHECK_VERSION(2, 60, 0)
/* This is actually a copy of the internal GLib function used to
 * implement g_log_writer_is_journald(), rather the public wrapper,
 * adapted to use GLib functions (rather than having to reimplement
 * them for use in gio-launch-desktop, as in GLib's version). */
gboolean
my_g_fd_is_journal (int output_fd)
{
  /* FIXME: Use the new journal API for detecting whether we’re writing to the
   * journal. See: https://github.com/systemd/systemd/issues/2473
   */
  union {
    struct sockaddr_storage storage;
    struct sockaddr sa;
    struct sockaddr_un un;
  } addr;
  socklen_t addr_len;
  int err;

  if (output_fd < 0)
    return 0;

  /* Namespaced journals start with `/run/systemd/journal.${name}/` (see
   * `RuntimeDirectory=systemd/journal.%i` in `systemd-journald@.service`. The
   * default journal starts with `/run/systemd/journal/`. */
  memset (&addr, 0, sizeof (addr));
  addr_len = sizeof(addr);
  err = getpeername (output_fd, &addr.sa, &addr_len);
  if (err == 0 && addr.storage.ss_family == AF_UNIX)
    return (g_str_has_prefix (addr.un.sun_path, "/run/systemd/journal/") ||
            g_str_has_prefix (addr.un.sun_path, "/run/systemd/journal."));

  return 0;
}
#endif

#if !GLIB_CHECK_VERSION(2, 68, 0)
/*
 * g_string_replace:
 * @string: a #GString
 * @find: the string to find in @string
 * @replace: the string to insert in place of @find
 * @limit: the maximum instances of @find to replace with @replace, or `0` for
 * no limit
 *
 * Replaces the string @find with the string @replace in a #GString up to
 * @limit times. If the number of instances of @find in the #GString is
 * less than @limit, all instances are replaced. If @limit is `0`,
 * all instances of @find are replaced.
 *
 * Returns: the number of find and replace operations performed.
 *
 * Since: 2.68
 */
guint
my_g_string_replace (GString     *string,
                     const gchar *find,
                     const gchar *replace,
                     guint        limit)
{
  gsize f_len, r_len, pos;
  gchar *cur, *next;
  guint n = 0;

  g_return_val_if_fail (string != NULL, 0);
  g_return_val_if_fail (find != NULL, 0);
  g_return_val_if_fail (replace != NULL, 0);

  f_len = strlen (find);
  r_len = strlen (replace);
  cur = string->str;

  while ((next = strstr (cur, find)) != NULL)
    {
      pos = next - string->str;
      g_string_erase (string, pos, f_len);
      g_string_insert (string, pos, replace);
      cur = string->str + pos + r_len;
      n++;
      if (n == limit)
        break;
    }

  return n;
}
#endif
