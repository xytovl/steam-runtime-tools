/*<private_header>*/
/*
 * Copyright 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "steam-runtime-tools/sdl-utils-internal.h"

#include <SDL_ttf.h>

int global_ttf_init(void);
void global_shutdown_ttf(void);

TTF_Font *ttf_load_font(const char *family,
                        const char *style,
                        int size);

static inline void clear_font(TTF_Font **p)
{
    clear_pointer(p, TTF_CloseFont);
}
