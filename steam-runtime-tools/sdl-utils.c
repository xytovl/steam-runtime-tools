/*
 * Copyright 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "sdl-utils-internal.h"
#include "sdl-ttf-utils-internal.h"

#include <errno.h>
#include <pwd.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>

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
 * Like SDL_strdup, but sets error if it returns NULL.
 */
static inline char *sdl_strdup_or_set_error(const char *s)
{
    char *ret;

    if (s == NULL) {
        SDL_InvalidParamError("s");
        return NULL;
    }

    ret = SDL_strdup(s);

    if (ret == NULL) {
        SDL_OutOfMemory();
    }

    return ret;
}

/*
 * Like g_get_home_dir(), but with SDL_malloc and SDL_SetError.
 */
static char *get_home_dir(char *buf,
                          size_t len)
{
    const char *home = NULL;
    struct passwd pwd;
    struct passwd *result;
    uid_t uid;

    home = SDL_getenv("HOME");

    if (home != NULL) {
        return sdl_strdup_or_set_error(home);
    }

    uid = getuid();

    if (getpwuid_r(uid, &pwd, buf, len, &result) != 0) {
        SDL_SetError("Failed to look up uid %lu: %s", (unsigned long) uid, strerror(errno));
        return NULL;
    }

    if (result == NULL) {
        SDL_SetError("uid %lu not found in system user database", (unsigned long) uid);
        return NULL;
    }

    if (result->pw_dir == NULL) {
        SDL_SetError("uid %lu has no home directory", (unsigned long) uid);
    }

    return sdl_strdup_or_set_error(result->pw_dir);
}

TTF_Font *ttf_load_steam_ui_font(const char *basename,
                                 int size)
{
    __attribute__((cleanup(clear_font))) TTF_Font *font = NULL;
    __attribute__((cleanup(clear_sdl_free))) char *home = NULL;
    autofree char *filename = NULL;
    /* Arbitrary size, probably enough for a line from /etc/passwd */
    char buf[4096];

    home = get_home_dir(buf, sizeof(buf));

    if (home == NULL) {
        return NULL;
    }

    if (asprintf(&filename, "%s/.steam/steam/clientui/fonts/%s", home, basename) < 0) {
        SDL_SetError("%s", strerror(errno));
        return NULL;
    }

    font = TTF_OpenFontIndex(filename, size, 0);

    if (font == NULL) {
        SDL_SetError ("Couldn't load font \"%s\" #0", filename);
        return NULL;
    }

    return steal_pointer(&font);
}

/*
 * Use fontconfig and SDL_ttf to load @family in style @style.
 * (Is it really meant to be this complicated?)
 *
 * Returns: NULL with SDL error set on failure
 */
TTF_Font *ttf_load_font_family(const char *family,
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
