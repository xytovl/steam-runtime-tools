/*<private_header>*/
/*
 * Copyright 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
#pragma GCC diagnostic ignored "-Wswitch-default"
#include <SDL.h>
#pragma GCC diagnostic pop

#include "libc-utils-internal.h"

int global_sdl_init (void);
void global_shutdown_sdl (void);

/*
 * Prefix a format string to the SDL error indicator
 *
 * Returns: -1
 */
#define prefix_sdl_error(format, ...) \
  SDL_SetError(format ": %s", ## __VA_ARGS__, SDL_GetError())

static inline void clear_renderer(SDL_Renderer **p)
{
    clear_pointer(p, SDL_DestroyRenderer);
}

static inline void clear_surface(SDL_Surface **p)
{
    clear_pointer(p, SDL_FreeSurface);
}

static inline void clear_texture(SDL_Texture **p)
{
    clear_pointer(p, SDL_DestroyTexture);
}

static inline void clear_window(SDL_Window **p)
{
    clear_pointer(p, SDL_DestroyWindow);
}
