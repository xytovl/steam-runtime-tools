/*
 * Copyright 2024 Patrick Nicolas <patricknicolas@laposte.net>
 * Copyright 2018-2021 Wim Taymans
 * Copyright 2021 Collabora Ltd.
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

#include "wrap-openxr.h"

#include <glib.h>

static const char* known_sockets[] = {
  "monado_comp_ipc", // https://gitlab.freedesktop.org/monado/monado/-/blob/faf0aafbd46d0d5c16f5a5683c3c871f3e0cac13/CMakeLists.txt#L339
  "wivrn/comp_ipc" // https://github.com/WiVRn/WiVRn/blob/a6977ca27f8e8362f19a5bd95efb85d195b6de1d/server/CMakeLists.txt#L57
};

static const char *
get_runtime_dir (void)
{
  const char *runtime_dir;

  runtime_dir = g_getenv ("XDG_RUNTIME_DIR");

  if (runtime_dir == NULL)
    runtime_dir = "/tmp";

  return runtime_dir;
}

/*
 * OpenXR runtimes often have a server process and use a socket
 * for clients to connect.
 * The OpenXR specification does not describe this, nor offers
 * a mechanism for runtimes to describe it.
 * Use a list of known socket names.
 */
void
pv_wrap_add_openxr_args (FlatpakBwrap *sharing_bwrap,
                         SrtEnvOverlay *container_env)
{
  const char *runtime_dir = get_runtime_dir ();
  int i;

  for (i = 0;
       i < G_N_ELEMENTS(known_sockets);
       i++)
    {
      g_autofree gchar *host_socket =
        g_build_filename (runtime_dir, known_sockets[i], NULL);

      g_debug ("testing OpenXR socket %s", host_socket);

      if (g_file_test (host_socket, G_FILE_TEST_EXISTS))
        {
          g_debug ("OpenXR socket %s found", host_socket);
          g_autofree gchar *container_socket =
            g_strdup_printf ("/run/user/%d/%s", getuid (), known_sockets[i]);

          flatpak_bwrap_add_args (sharing_bwrap,
                                  "--ro-bind",
                                  host_socket,
                                  container_socket,
                                  NULL);
        }
    }
}
