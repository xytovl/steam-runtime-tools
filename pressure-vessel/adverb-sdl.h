/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "libglnx.h"

#include <glib.h>

#include "steam-runtime-tools/steam-internal.h"

#include "flatpak-bwrap-private.h"
#include "per-arch-dirs.h"

gboolean pv_adverb_set_up_dynamic_sdl (FlatpakBwrap *wrapped_command,
                                       PvPerArchDirs *lib_temp_dirs,
                                       const char *prefix,
                                       const char *overrides,
                                       const char *dynamic_var,
                                       const char *soname,
                                       GError **error);

void pv_adverb_set_up_dynamic_sdls (FlatpakBwrap *wrapped_command,
                                    PvPerArchDirs *lib_temp_dirs,
                                    const char *prefix,
                                    const char *overrides,
                                    SrtSteamCompatFlags compat_flags);
