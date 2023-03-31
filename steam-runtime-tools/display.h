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

#if !defined(_SRT_IN_SINGLE_HEADER) && !defined(_SRT_COMPILATION)
#error "Do not include directly, use <steam-runtime-tools/steam-runtime-tools.h>"
#endif

#include <glib.h>
#include <glib-object.h>

#include <steam-runtime-tools/macros.h>

typedef struct _SrtDisplayInfo SrtDisplayInfo;
typedef struct _SrtDisplayInfoClass SrtDisplayInfoClass;

#define SRT_TYPE_DISPLAY_INFO srt_display_info_get_type ()
#define SRT_DISPLAY_INFO(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_DISPLAY_INFO, SrtDisplayInfo))
#define SRT_DISPLAY_INFO_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_DISPLAY_INFO, SrtDisplayClass))
#define SRT_IS_DISPLAY_INFO(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_DISPLAY_INFO))
#define SRT_IS_DISPLAY_INFO_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_DISPLAY_INFO))
#define SRT_DISPLAY_INFO_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_DISPLAY_INFO, SrtDisplayClass)

_SRT_PUBLIC
GType srt_display_info_get_type (void);

/**
 * SrtDisplayWaylandIssues:
 * @SRT_DISPLAY_WAYLAND_ISSUES_NONE: There are no problems
 * @SRT_DISPLAY_WAYLAND_ISSUES_UNKNOWN: A generic internal error occurred while
 *  trying to check the Wayland display session
 * @SRT_DISPLAY_WAYLAND_ISSUES_MISSING_SOCKET: The Wayland socket in
 *  $WAYLAND_DISPLAY, or if unset the default `wayland-0`, is missing
 */
typedef enum
{
  SRT_DISPLAY_WAYLAND_ISSUES_NONE = 0,
  SRT_DISPLAY_WAYLAND_ISSUES_UNKNOWN = (1 << 0),
  SRT_DISPLAY_WAYLAND_ISSUES_MISSING_SOCKET = (1 << 1),
} SrtDisplayWaylandIssues;

/**
 * SrtDisplayX11Type:
 * @SRT_DISPLAY_X11_TYPE_UNKNOWN: Unknown X11 display type
 * @SRT_DISPLAY_X11_TYPE_MISSING: There isn't an X11 display
 * @SRT_DISPLAY_X11_TYPE_NATIVE: There is a native X11 display server
 * @SRT_DISPLAY_X11_TYPE_XWAYLAND: The X11 display server is Xwayland
 */
typedef enum
{
  SRT_DISPLAY_X11_TYPE_UNKNOWN = 0,
  SRT_DISPLAY_X11_TYPE_MISSING,
  SRT_DISPLAY_X11_TYPE_NATIVE,
  SRT_DISPLAY_X11_TYPE_XWAYLAND,
} SrtDisplayX11Type;

_SRT_PUBLIC
const gchar * const *srt_display_info_get_environment_list (SrtDisplayInfo *self);
_SRT_PUBLIC
gboolean srt_display_info_is_wayland_session (SrtDisplayInfo *self);
_SRT_PUBLIC
SrtDisplayWaylandIssues srt_display_info_get_wayland_issues (SrtDisplayInfo *self);
_SRT_PUBLIC
SrtDisplayX11Type srt_display_info_get_x11_type (SrtDisplayInfo *self);
_SRT_PUBLIC
const char *srt_display_info_get_x11_messages (SrtDisplayInfo *self);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtDisplayInfo, g_object_unref)
#endif
