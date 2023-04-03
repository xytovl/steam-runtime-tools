/*
 * Copyright 2017 Discord
 * Copyright 2021-2023 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "wrap-discord.h"

/* Adapted from discord-rpc v3.4.0 */
static const char *
get_temp_dir (void)
{
  const char *temp_dir;

  temp_dir = g_getenv ("XDG_RUNTIME_DIR");

  if (temp_dir == NULL)
    temp_dir = g_getenv ("TMPDIR");

  if (temp_dir == NULL)
    temp_dir = g_getenv ("TMP");

  if (temp_dir == NULL)
    temp_dir = g_getenv ("TEMP");

  if (temp_dir == NULL)
    temp_dir = "/tmp";

  return temp_dir;
}

void
pv_wrap_add_discord_args (FlatpakBwrap *sharing_bwrap)
{
  g_autoptr(GDir) dir = NULL;
  const char *temp_dir = get_temp_dir ();
  const char *member;

  dir = g_dir_open (temp_dir, 0, NULL);

  if (dir == NULL)
    return;

  for (member = g_dir_read_name (dir);
       member != NULL;
       member = g_dir_read_name (dir))
    {
      /* Bind the Discord Rich Presence IPC sockets. They are expected to be
       * called `discord-ipc-`, followed by a number.
        */
      if (g_str_has_prefix (member, "discord-ipc-"))
        {
          g_autofree gchar *host_socket =
            g_build_filename (temp_dir, member, NULL);
          g_autofree gchar *container_socket =
            g_strdup_printf ("/run/user/%d/%s", getuid (), member);

          flatpak_bwrap_add_args (sharing_bwrap,
                                  "--ro-bind",
                                    host_socket,
                                    container_socket,
                                  NULL);
        }
    }
}
