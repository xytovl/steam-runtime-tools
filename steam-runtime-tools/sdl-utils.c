/*
 * Copyright 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "sdl-utils-internal.h"
#include "sdl-ttf-utils-internal.h"

#include <stdbool.h>

#include <dbus/dbus.h>
#include <fontconfig/fontconfig.h>

#include "libc-utils-internal.h"

static inline void clear_config(FcConfig **p)
{
    clear_pointer(p, FcConfigDestroy);
}

static inline void clear_pattern(FcPattern **p)
{
    clear_pointer(p, FcPatternDestroy);
}

static inline void clear_object_set(FcObjectSet **p)
{
    clear_pointer(p, FcObjectSetDestroy);
}

static inline void clear_font_set(FcFontSet **p)
{
    clear_pointer(p, FcFontSetDestroy);
}

static bool global_sdl_inited = false;
static bool global_ttf_inited = false;
static bool global_fontconfig_inited = false;

/*
 * Use fontconfig and SDL_ttf to load @family in style @style.
 * (Is it really meant to be this complicated?)
 *
 * Returns: NULL with SDL error set on failure
 */
TTF_Font *ttf_load_font(const char *family,
                        const char *style,
                        int size)
{
    __attribute__((cleanup(clear_config))) FcConfig *config = NULL;
    __attribute__((cleanup(clear_pattern))) FcPattern *pattern = NULL;
    __attribute__((cleanup(clear_object_set))) FcObjectSet *object_set = NULL;
    __attribute__((cleanup(clear_font_set))) FcFontSet *font_set = NULL;
    __attribute__((cleanup(clear_font_set))) FcFontSet *sorted = NULL;
    __attribute__((cleanup(clear_font))) TTF_Font *font = NULL;
    const char *filename;
    int i;
    FcResult result;

    config = FcInitLoadConfigAndFonts();
    global_fontconfig_inited = true;

    if (config == NULL) {
        SDL_SetError("Failed to initialize fontconfig");
        return NULL;
    }

    object_set = FcObjectSetBuild(FC_FILE, FC_INDEX, NULL);

    if (object_set == NULL) {
        SDL_SetError("Failed to allocate object set");
        return NULL;
    }

    pattern = FcPatternCreate();

    if (pattern == NULL) {
        SDL_SetError("Failed to allocate pattern");
        return NULL;
    }

    FcPatternAddString(pattern, FC_FAMILY, (const FcChar8 *) family);
    FcPatternAddString(pattern, FC_FONTFORMAT, (const FcChar8 *) "TrueType");

    if (style != NULL) {
        FcPatternAddString(pattern, FC_STYLE, (const FcChar8 *) style);
    }

    FcConfigSubstitute(config, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    font_set = FcFontSetCreate();

    if (font_set == NULL) {
        SDL_SetError("Failed to allocate font set");
        return NULL;
    }

    sorted = FcFontSort(config, pattern, FcTrue, 0, &result);

    if (sorted == NULL || sorted->nfont == 0) {
        SDL_SetError ("Couldn't find any fonts");
        return NULL;
    }

    for (i = 0; i < sorted->nfont; i++) {
        FcPattern *p = FcFontRenderPrepare(config, pattern, sorted->fonts[i]);

        if (p != NULL) {
            FcFontSetAdd(font_set, p);
        }
    }

    for (i = 0; i < font_set->nfont; i++) {
        FcValue value;

        if (FcPatternGet(font_set->fonts[i], FC_FILE, 0, &value) == FcResultMatch
            && value.type == FcTypeString) {
            long font_index = 0;

            filename = (const char *) value.u.s;

            if (FcPatternGet(font_set->fonts[i], FC_INDEX, 0, &value) == FcResultMatch
                && value.type == FcTypeInteger) {
                font_index = value.u.i;
            }

            font = TTF_OpenFontIndex(filename, size, font_index);

            if (font == NULL) {
                SDL_SetError ("Couldn't load font \"%s\" #%ld", filename, font_index);
                return NULL;
            }

            return steal_pointer (&font);
        }
    }

    SDL_SetError("Couldn't find font \"%s\"", family);
    return NULL;
}

/*
 * Initialize SDL, but only once
 *
 * Returns: 0 on success, -1 with SDL error set on failure
 */
int global_sdl_init(void)
{
    if (global_sdl_inited) {
        return 0;
    }

    if (SDL_Init (SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) < 0) {
        return prefix_sdl_error ("Failed to initialize SDL");
    }

    global_sdl_inited = true;
    return 0;
}

/*
 * Initialize SDL_ttf, but only once
 *
 * Returns: 0 on success, -1 with SDL error set on failure
 */
int global_ttf_init(void)
{
    global_sdl_init ();

    if (global_ttf_inited) {
        return 0;
    }

    if (TTF_Init() < 0) {
        return prefix_sdl_error ("Failed to initialize SDL_ttf");
    }

    global_ttf_inited = true;
    return 0;
}

/*
 * Shut down SDL_ttf and fontconfig. Can only be called at the end of main(),
 * while no more threads are using SDL_ttf.
 */
void global_shutdown_ttf(void)
{
    if (global_ttf_inited) {
        TTF_Quit();
        global_ttf_inited = false;
    }

    if (global_fontconfig_inited) {
        FcFini();
        global_fontconfig_inited = false;
    }
}

/*
 * Shut down SDL and libdbus. Can only be called at the end of main(),
 * while no more threads are using SDL, libdbus, or anything with a plugin
 * architecture that might call into libdbus.
 */
void global_shutdown_sdl(void)
{
    if (global_sdl_inited) {
        SDL_Quit();
        dbus_shutdown();
        global_sdl_inited = false;
    }
}
