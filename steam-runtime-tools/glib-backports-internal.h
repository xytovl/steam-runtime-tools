/*<private_header>*/
/*
 * Functions backported and adapted from GLib
 *
 *  Copyright 2000 Red Hat, Inc.
 *  Copyright 2019 Collabora Ltd.
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

#include <libglnx.h>

#if !GLIB_CHECK_VERSION(2, 60, 0)
/* This is actually present in 2.58.x, but it's buggy:
 * https://gitlab.gnome.org/GNOME/glib/-/issues/1589 */
#define g_log_writer_is_journald(fd) my_g_fd_is_journal(fd)
gboolean my_g_fd_is_journal (int output_fd);
#endif

#if !GLIB_CHECK_VERSION(2, 64, 0)
#if defined(G_HAVE_ISO_VARARGS) && (!defined(G_ANALYZER_ANALYZING) || !G_ANALYZER_ANALYZING)
#define g_warning_once(...) \
  G_STMT_START { \
    static int G_PASTE (_GWarningOnceBoolean, __LINE__) = 0;  /* (atomic) */ \
    if (g_atomic_int_compare_and_exchange (&G_PASTE (_GWarningOnceBoolean, __LINE__), \
                                           0, 1)) \
      g_warning (__VA_ARGS__); \
  } G_STMT_END
#elif defined(G_HAVE_GNUC_VARARGS) && (!defined(G_ANALYZER_ANALYZING) || !G_ANALYZER_ANALYZING)
#define g_warning_once(format...) \
  G_STMT_START { \
    static int G_PASTE (_GWarningOnceBoolean, __LINE__) = 0;  /* (atomic) */ \
    if (g_atomic_int_compare_and_exchange (&G_PASTE (_GWarningOnceBoolean, __LINE__), \
                                           0, 1)) \
      g_warning (format); \
  } G_STMT_END
#else
#define g_warning_once g_warning
#endif
#endif

#if !GLIB_CHECK_VERSION(2, 68, 0)
#define g_string_replace(s,f,r,l) my_g_string_replace (s, f, r, l)
guint my_g_string_replace (GString *string,
                           const gchar *find,
                           const gchar *replace,
                           guint limit);
#endif

#if !GLIB_CHECK_VERSION(2, 70, 0)
/* In GLib 2.34 to 2.68, this was available under a misleading name */
#define g_spawn_check_wait_status g_spawn_check_exit_status
#endif
