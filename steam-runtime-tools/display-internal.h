/*<private_header>*/
/*
 * Copyright © 2023 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/steam-runtime-tools.h"

#include <json-glib/json-glib.h>

/*
 * _srt_display_info_new:
 * @display_environ: (nullable): Environment variables relevant to the display server
 * @wayland_session: %TRUE if this is a Wayland session
 * @wayland_issues: Problems with Wayland
 *
 * Inline convenience function to create a new SrtDisplayInfo.
 * This is not part of the public API.
 *
 * Returns: (transfer full): A new #SrtDisplayInfo
 */
static inline SrtDisplayInfo *_srt_display_info_new (GStrv display_environ,
                                                     gboolean wayland_session,
                                                     SrtDisplayWaylandIssues wayland_issues);

#ifndef __GTK_DOC_IGNORE__
static inline SrtDisplayInfo *
_srt_display_info_new (GStrv display_environ,
                       gboolean wayland_session,
                       SrtDisplayWaylandIssues wayland_issues)
{
  return g_object_new (SRT_TYPE_DISPLAY_INFO,
                       "display-environ", display_environ,
                       "wayland-session", wayland_session,
                       "wayland-issues", wayland_issues,
                       NULL);
}
#endif

SrtDisplayInfo *_srt_check_display (gchar **envp);

SrtDisplayInfo *_srt_display_info_get_from_report (JsonObject *json_obj);
