/*
 * steam-runtime-dialog-ui: Basic implementation of a Zenity-like UI
 * Copyright 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "steam-runtime-tools/libc-utils-internal.h"
#include "steam-runtime-tools/sdl-ttf-utils-internal.h"
#include "steam-runtime-tools/sdl-utils-internal.h"

#include <SDL.h>
#include <SDL_syswm.h>
#include <X11/Xlib.h>

#define THIS_PROGRAM "steam-runtime-dialog-ui"

#define trace(...) SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__);
#define debug(...) SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__);
#define info(...) SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__);

typedef int (*Failable) (void);

static int check_features (char *features)
{
    char *saveptr = NULL;
    char *token;

    if (strcmp0(getenv("XDG_CURRENT_DESKTOP"), "gamescope") == 0) {
        return SDL_SetError("This implementation does not yet work under Gamescope");
    }

    for (token = strtok_r(features, " \t\n", &saveptr);
         token != NULL;
         token = strtok_r(NULL, " \t\n", &saveptr)) {
        switch (token[0]) {
            case '\0':
                continue;

            case 'm':
                if (strcmp(token, "message") == 0) {
                    continue;
                }
                break;

            case 'p':
                if (strcmp(token, "progress") == 0) {
                    continue;
                }
                break;

            default:
                break;
        }

        return SDL_SetError("Unsupported feature \"%s\"", token);
    }

    return 0;
}

static bool opt_auto_close = false;
static bool opt_cancel = true;
static long opt_height = 0;
static Failable opt_mode = NULL;
static double opt_percentage = 0.0;
static bool opt_pulsate = false;
static const char *opt_text = NULL;
static const char *opt_title = NULL;
static unsigned opt_verbosity = 0;
static long opt_width = 0;
static bool opt_wrap = true;

typedef struct
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    TTF_Font *title_font;
    TTF_Font *message_font;
    char *title;
    SDL_Surface *title_surface;
    SDL_Texture *title_texture;
    char *message;
    SDL_Surface *message_surface;
    SDL_Texture *message_texture;
    SDL_GameController **controllers;
    size_t n_controllers;
    float progress;
    int w;
    int h;
    bool pulsate;
    bool did_app_id;
} Dialog;

static void dialog_free(Dialog *self)
{
    size_t i;

    free(self->message);
    clear_pointer(&self->message_surface, SDL_FreeSurface);
    clear_pointer(&self->message_texture, SDL_DestroyTexture);
    clear_pointer(&self->message_font, TTF_CloseFont);

    free(self->title);
    clear_pointer(&self->title_surface, SDL_FreeSurface);
    clear_pointer(&self->title_texture, SDL_DestroyTexture);
    clear_pointer(&self->title_font, TTF_CloseFont);

    clear_pointer(&self->renderer, SDL_DestroyRenderer);
    clear_pointer(&self->window, SDL_DestroyWindow);
    for (i = 0; i < self->n_controllers; i++) {
        clear_pointer(&self->controllers[i], SDL_GameControllerClose);
    }
    free(self->controllers);
    free(self);
}

static void clear_dialog(Dialog **p)
{
    clear_pointer(p, dialog_free);
}

static int dialog_set_title(Dialog *self,
                            const char *title)
{
    SDL_Color foreground = { 0xFF, 0xFF, 0xFF, SDL_ALPHA_OPAQUE };
    SDL_Surface *surface = NULL;

    if (title == NULL) {
        title = "";
    }

    if (strcmp0(title, self->title) != 0) {
        clear_pointer(&self->title, free);
        self->title = xstrdup(title);
    }

    surface = TTF_RenderUTF8_Blended(self->title_font, title, foreground);

    if (surface == NULL) {
        return prefix_sdl_error("Failed to render title");
    }

    clear_pointer (&self->title_surface, SDL_FreeSurface);
    clear_pointer (&self->title_texture, SDL_DestroyTexture);
    self->title_surface = steal_pointer (&surface);
    self->title_texture = SDL_CreateTextureFromSurface(self->renderer,
                                                       self->title_surface);
    return 0;
}

static int dialog_set_message(Dialog *self,
                              const char *message)
{
    SDL_Color foreground = { 0xFF, 0xFF, 0xFF, SDL_ALPHA_OPAQUE };
    SDL_Surface *surface = NULL;
    int w, h;

    if (message == NULL) {
        message = "";
    }

    if (strcmp0(message, self->message) != 0) {
        clear_pointer(&self->message, free);
        self->message = xstrdup(message);
    }

    SDL_GetRendererOutputSize(self->renderer, &w, &h);

    surface = TTF_RenderUTF8_Blended_Wrapped(self->message_font, message,
                                             foreground, 0.8 * w);

    if (surface == NULL) {
        return prefix_sdl_error ("Failed to render message");
    }

    clear_pointer(&self->message_surface, SDL_FreeSurface);
    clear_pointer(&self->message_texture, SDL_DestroyTexture);
    self->message_surface = steal_pointer (&surface);
    self->message_texture = SDL_CreateTextureFromSurface(self->renderer,
                                                         self->message_surface);
    return 0;
}

static void
dialog_set_app_id(Dialog *self)
{
    /* Steam game ID of the Steam client UI.
     * Note that this needs to be a long because that's how Xlib represents
     * CARDINAL properties, even though a CARDINAL is only 32 bits of
     * valid data! */
    static const unsigned long appid = 769;

    SDL_SysWMinfo info = {
        .version = { .major = 2, .minor = 0, .patch = 0 }
    };
    Atom appid_atom;
    int res;

    if (self->did_app_id) {
        return;
    }

    self->did_app_id = true;

    /* Make our binary more portable to old SDL by asking for the
     * SysWMinfo that was provided by SDL 2.0: we know that the display
     * and window members were added in 2.0 and haven't subsequently
     * changed. */
#if 0
    SDL_VERSION(&info.version);
#endif

    /* If running under Gamescope, tell it to display our window as though
     * it was part of the Steam user interface */
    if (!SDL_GetWindowWMInfo(self->window, &info)) {
        warnx("Unable to get window management info: %s", SDL_GetError());
        return;
    }

    if (info.subsystem != SDL_SYSWM_X11) {
        debug("Not running under X11, cannot set STEAM_GAME");
        return;
    }

    appid_atom = XInternAtom(info.info.x11.display, "STEAM_GAME", False);

    if (appid_atom == None) {
        warnx("Unable to create X11 Atom for STEAM_GAME");
        return;
    }

    res = XChangeProperty(info.info.x11.display, info.info.x11.window,
                          appid_atom, XA_CARDINAL, 32, PropModeReplace,
                          (unsigned char *) &appid, 1);
    debug("Set property STEAM_GAME=%lu -> %d", appid, res);

#if 0
    Atom type_out = None;
    int format_out = -1;
    unsigned long n_out = 0;
    unsigned long bytes_remaining_out = 0;
    unsigned char *prop_out = NULL;
    unsigned long value = 0;

    res = XGetWindowProperty(info.info.x11.display, info.info.x11.window,
                             appid_atom, 0, 1, False, XA_CARDINAL,
                             &type_out, &format_out, &n_out,
                             &bytes_remaining_out, &prop_out);
    debug("Get STEAM_GAME -> %d", res);
    debug("STEAM_GAME has %lu %d-bit Atom(%lu) values and %lu bytes more",
          n_out, format_out, type_out, bytes_remaining_out);
    memcpy(&value, prop_out, sizeof(value));
    debug("STEAM_GAME = %lu", value);
    XFree(prop_out);
#endif
}

static void dialog_draw_frame(Dialog *self)
{
    int w, h;

    SDL_GetRendererOutputSize(self->renderer, &w, &h);

    if (w != self->w || h != self->h) {
        self->w = w;
        self->h = h;
        dialog_set_title (self, self->title);
        dialog_set_message (self, self->message);
    }

    /* opaque black */
    SDL_SetRenderDrawColor(self->renderer, 0x00, 0x00, 0x00, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(self->renderer);

    if (self->title_texture != NULL) {
        SDL_FRect rect = {};

        rect.x = self->w * 0.1;
        rect.w = self->title_surface->w;
        rect.y = self->h * 0.1;
        rect.h = self->title_surface->h;

        SDL_RenderCopyF(self->renderer, self->title_texture, NULL, &rect);
    }

    if (self->pulsate || self->progress >= 0.0f) {
        SDL_FRect rect = {};

        rect.x = self->w * 0.1;
        rect.w = self->w * 0.8;
        rect.y = self->h * 0.2;
        rect.h = self->h * 0.025;

        SDL_SetRenderDrawColor(self->renderer, 0x66, 0x66, 0x66, SDL_ALPHA_OPAQUE);
        SDL_RenderFillRectF(self->renderer, &rect);

        if (self->pulsate) {
            Uint32 timestamp = SDL_GetTicks () % 1000;

            rect.x = rect.x + (rect.w * timestamp / 1100.0f);
            rect.w = rect.w * 100.0f / 1100.0f;
        } else {
            rect.w = (rect.w * self->progress) / 100.0f;
        }

        SDL_SetRenderDrawColor(self->renderer, 0xcc, 0xcc, 0xcc, SDL_ALPHA_OPAQUE);
        SDL_RenderFillRectF(self->renderer, &rect);
    }

    if (self->message_texture != NULL) {
        SDL_FRect rect = {};

        rect.x = self->w * 0.1;
        rect.w = self->message_surface->w;
        rect.y = self->h * 0.3;
        rect.h = self->message_surface->h;

        SDL_RenderCopyF(self->renderer, self->message_texture, NULL, &rect);
    }

    SDL_RenderPresent(self->renderer);
    SDL_ShowWindow(self->window);

    dialog_set_app_id(self);
}

static void
dialog_add_controller(Dialog *self,
                      int joystick_index)
{
    SDL_JoystickID instance_id = SDL_JoystickGetDeviceInstanceID(joystick_index);
    size_t i;
    size_t unused_index = SIZE_MAX;

    for (i = 0; i < self->n_controllers; i++) {
        if (self->controllers[i] == NULL) {
            unused_index = i;
            continue;
        }

        if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(self->controllers[i])) == instance_id) {
            info("Not adding duplicate joystick %d", instance_id);
            return;
        }
    }

    if (unused_index == SIZE_MAX) {
        unused_index = self->n_controllers;
        self->n_controllers++;
        self->controllers = xrealloc(self->controllers, self->n_controllers * sizeof(SDL_GameController *));
    }

    info("Adding joystick %d at index %zu", instance_id, unused_index);
    self->controllers[unused_index] = SDL_GameControllerOpen(joystick_index);

    if (self->controllers[unused_index] == NULL) {
        warnx("%s", SDL_GetError());
    }
}

static void
dialog_remove_controller(Dialog *self,
                         SDL_JoystickID instance_id)
{
    size_t i;

    for (i = 0; i < self->n_controllers; i++) {
        if (self->controllers[i] == NULL) {
            continue;
        }

        if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(self->controllers[i])) != instance_id) {
            continue;
        }

        info("Removing joystick %d from index %zu", instance_id, i);
        clear_pointer(&self->controllers[i], SDL_GameControllerClose);
    }
}

static void
dialog_open_input(Dialog *self)
{
    int i;

    for (i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            dialog_add_controller(self, i);
        }
    }
}

/*
 * Returns: true if we should continue to run
 */
static bool
dialog_handle_input(Dialog *self,
                    const SDL_Event *event)
{
    switch (event->type) {
        case SDL_CONTROLLERDEVICEADDED:
            dialog_add_controller(self, event->cdevice.which);
            return true;

        case SDL_CONTROLLERDEVICEREMOVED:
            dialog_remove_controller(self, event->cdevice.which);
            return true;

        case SDL_CONTROLLERBUTTONDOWN:
            info("Controller %d button %d pressed",
                 event->cbutton.which, event->cbutton.button);
            return false;

        case SDL_KEYDOWN:
            info("Key %d pressed", event->key.keysym.sym);
            return false;

        case SDL_MOUSEBUTTONDOWN:
            info("Mouse button pressed");
            return false;

        case SDL_QUIT:
            info("Window closed");
            return false;

        default:
            return true;
    }
}

static Dialog *
dialog_new(const char *title)
{
    __attribute__((cleanup(clear_dialog))) Dialog *self = NULL;
    unsigned flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE;
    int width;
    int height;

    if (global_sdl_init() < 0)
        return NULL;

    if (global_ttf_init() < 0)
        return NULL;

    if (opt_width >= 320 && opt_width <= 3200) {
        width = opt_width;
    } else {
        width = 640;
    }

    if (opt_height >= 240 && opt_width <= 2400) {
        height = opt_height;
    } else {
        height = 480;
    }

    self = new0(Dialog);
    self->progress = -1.0f;

    if (strcmp0(getenv("XDG_CURRENT_DESKTOP"), "gamescope") == 0
        || getenv_bool("STEAM_RUNTIME_DIALOG_FULLSCREEN", false)) {
        info("Going to full-screen");
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    self->window = SDL_CreateWindow(title,
                                    SDL_WINDOWPOS_CENTERED,
                                    SDL_WINDOWPOS_CENTERED,
                                    width,
                                    height,
                                    flags);

    if (self->window == NULL) {
        prefix_sdl_error("Failed to create window");
        return NULL;
    }

    self->renderer = SDL_CreateRenderer(self->window, -1, 0);

    if (self->renderer == NULL) {
        prefix_sdl_error("Failed to create renderer");
        return NULL;
    }

    SDL_GetRendererOutputSize(self->renderer, &self->w, &self->h);

    self->title_font = ttf_load_font("sans-serif", "bold",
                                     (24.0 * self->h) / 480.0);

    if (self->title_font == NULL) {
        self->title_font = ttf_load_font("sans-serif", NULL,
                                         (24.0 * self->h) / 480.0);

        if (self->title_font != NULL) {
            TTF_SetFontStyle(self->title_font, TTF_STYLE_BOLD);
        }
    }

    if (self->title_font == NULL) {
        prefix_sdl_error("Failed to load title font");
        return NULL;
    }

    self->message_font = ttf_load_font("sans-serif", NULL,
                                       (18.0 * self->h) / 480.0);

    if (self->message_font == NULL) {
        prefix_sdl_error ("Failed to load message font");
        return NULL;
    }

    return steal_pointer(&self);
}

static int
do_message(const char *title)
{
    __attribute__((cleanup(clear_dialog))) Dialog *dialog = NULL;
    bool done = false;
    SDL_Event event = {};

    if (opt_title != NULL) {
        title = opt_title;
    }

    dialog = dialog_new(title);

    if (dialog == NULL) {
        return -1;
    }

    dialog_open_input(dialog);

    if (dialog_set_title(dialog, title) < 0) {
        return -1;
    }

    if (opt_text != NULL && dialog_set_message(dialog, opt_text) < 0) {
        return -1;
    }

    while (!done) {
        dialog_draw_frame(dialog);

        if (!SDL_WaitEvent(&event))
            return prefix_sdl_error ("Failed to get next event");

        if (!dialog_handle_input(dialog, &event)) {
            done = true;
        }
    }

    while (SDL_PollEvent(&event)) {
        /* ignore, just drain the queue */
    }

    return 0;
}

static int do_error(void)
{
    return do_message("Error");
}

static int do_info(void)
{
    return do_message("Notice");
}

static int do_warning(void)
{
    return do_message ("Warning");
}

typedef struct
{
  Uint32 stdin_event;
  int pipe_from_main;
} StdinWatch;

static int watch_stdin(void *data)
{
    const StdinWatch *watch_data = data;

    while (true) {
        SDL_UserEvent event = {};
        struct pollfd fds[2] = {};
        int res;

        fds[0].fd = STDIN_FILENO;
        fds[0].events = POLLIN;
        fds[0].revents = 0;

        fds[1].fd = watch_data->pipe_from_main;
        fds[1].events = POLLIN;
        fds[1].revents = 0;

        res = poll(fds, 2, -1);

        event.type = watch_data->stdin_event;
        event.timestamp = SDL_GetTicks();
        SDL_PushEvent((SDL_Event *) &event);

        if (res < 0)
            return res;

        if (fds[0].revents != POLLIN) {
            return fds[0].revents;
        }

        if (fds[1].revents != 0) {
            return 0;
        }
    }
}

static int do_progress(void)
{
    const char *title = NULL;
    __attribute__((cleanup(clear_dialog))) Dialog *dialog = NULL;
    autofree char *input = NULL;
    size_t input_len = 0;
    bool done = false;
    bool eof_stdin = false;
    StdinWatch watch_data =
    {
      .stdin_event = 0,
    };
    SDL_Thread *stdin_thread;
    int flags;
    SDL_Event event = {};
    int pipefds[2] = { -1, -1 };

    if (opt_title != NULL) {
        title = opt_title;
    }

    dialog = dialog_new(title);

    if (dialog == NULL) {
        return -1;
    }

    dialog->progress = opt_percentage;

    if (dialog->progress < 0.0f) {
        dialog->progress = 0.0f;
    }

    if (dialog->progress > 100.0f) {
        dialog->progress = 100.0f;
    }

    dialog->pulsate = !!opt_pulsate;

    if (opt_cancel) {
        dialog_open_input(dialog);
    }

    if (title != NULL && dialog_set_title(dialog, title) < 0) {
        return -1;
    }

    if (opt_text != NULL && dialog_set_message(dialog, opt_text) < 0) {
        return -1;
    }

    watch_data.stdin_event = SDL_RegisterEvents(1);

    if (watch_data.stdin_event <= 0)
        return prefix_sdl_error("Failed to register user events");

    flags = fcntl(STDIN_FILENO, F_GETFL);

    if (flags < 0) {
        return SDL_SetError ("Unable to get flags from standard input: %s",
                             strerror (errno));
    }

    if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) {
        return SDL_SetError ("Unable to set standard input non-blocking: %s",
                             strerror (errno));
    }

    if (pipe(pipefds) != 0) {
        return SDL_SetError ("Unable to open pipe-to-self: %s",
                             strerror (errno));
    }

    watch_data.pipe_from_main = steal_fd (&pipefds[0]);

    stdin_thread = SDL_CreateThread(watch_stdin, "read stdin",
                                    &watch_data);

    while (!done) {
        dialog_draw_frame(dialog);

        if (dialog->pulsate) {
            if (!SDL_WaitEventTimeout (&event, 1000/30)) {
                continue;
            }
        } else if (!SDL_WaitEvent (&event)) {
            return prefix_sdl_error ("Failed to get next event");
        }

        if (event.type == watch_data.stdin_event && !eof_stdin) {
            const unsigned LINE = 1024;
            ssize_t res;
            char *newline;

            do {
                size_t len = input_len;

                /* +1 for a \0 afterwards */
                input = xrealloc(input, input_len + LINE + 1);
                res = read(STDIN_FILENO, input + len, LINE);

                if (res < 0 && errno == EAGAIN) {
                    /* nothing more to read, stop for now */
                } else if (res > 0) {
                    input_len += res;
                    input[input_len] = '\0';
                } else {
                    eof_stdin = true;
                }
            } while (res > 0);

            if (eof_stdin) {
                /* Notify the stdin-watching thread to say we're done */
                close(pipefds[1]);
                pipefds[1] = -1;

                SDL_WaitThread(stdin_thread, NULL);
                stdin_thread = NULL;

                if (opt_auto_close) {
                    done = true;
                }
            }

            while (input_len > 0) {
                newline = memchr(input, '\n', input_len);

                if (newline != NULL) {
                    *newline = '\0';
                }

                if (newline != NULL || eof_stdin) {
                    if (input[0] == '#') {
                        dialog_set_message (dialog, input + 1);
                    } else if (strcmp(input, "pulsate:false") == 0) {
                        dialog->pulsate = false;
                    } else if (str_has_prefix(input, "pulsate:")) {
                        dialog->pulsate = true;
                    } else {
                        char *endptr;
                        double progress = strtod(input, &endptr);

                        if (endptr != NULL
                            && *endptr == '\0'
                            && input != endptr
                            && progress >= 0.0
                            && progress <= 100.0) {
                            dialog->progress = progress;
                        }

                        if (opt_auto_close && dialog->progress >= 100.0f) {
                            done = true;
                        }
                    }

                    if (newline != NULL) {
                        size_t remaining = input_len - (newline + 1 - input);

                        memmove(input,
                                newline + 1,
                                /* +1 for the \0, which is not counted */
                                remaining + 1);
                        input_len = remaining;
                    } else {
                        input_len = 0;
                    }
                }
            }
        }

        if (opt_cancel && !dialog_handle_input(dialog, &event)) {
            done = true;
        }
    }

    while (SDL_PollEvent(&event)) {
        /* ignore, just drain the queue */
    }

    return 0;
}

static int set_mode(Failable impl)
{
    if (opt_mode != NULL) {
        return SDL_SetError ("Cannot specify more than one mode argument");
    }

    opt_mode = impl;
    return 0;
}

static void usage(int code) __attribute__((__noreturn__));
static void usage(int code)
{
    FILE *stream = (code == EXIT_SUCCESS ? stdout : stderr);

    fprintf(stream, "Usage: %s [OPTIONS]\n", THIS_PROGRAM);
    fprintf(stream, "Options are the same as for steam-runtime-dialog.\n");
    exit(code);
}

enum
{
    OPTION_VERBOSE = 'v',
    OPTION_HELP = 1,
    OPTION_AUTO_CLOSE,
    OPTION_CHECK_FEATURES,
    OPTION_ERROR,
    OPTION_HEIGHT,
    OPTION_INFO,
    OPTION_NO_CANCEL,
    OPTION_NO_WRAP,
    OPTION_PERCENTAGE,
    OPTION_PROGRESS,
    OPTION_PULSATE,
    OPTION_TEXT,
    OPTION_TITLE,
    OPTION_VERSION,
    OPTION_WARNING,
    OPTION_WIDTH,
};

static struct option long_options[] =
{
    { "help", no_argument, NULL, OPTION_HELP },
    { "auto-close", no_argument, NULL, OPTION_AUTO_CLOSE },
    { "check-features", required_argument, NULL, OPTION_CHECK_FEATURES },
    { "error", no_argument, NULL, OPTION_ERROR },
    { "height", required_argument, NULL, OPTION_HEIGHT },
    { "info", no_argument, NULL, OPTION_INFO },
    { "no-cancel", no_argument, NULL, OPTION_NO_CANCEL },
    { "no-wrap", no_argument, NULL, OPTION_NO_WRAP },
    { "percentage", required_argument, NULL, OPTION_PERCENTAGE },
    { "progress", no_argument, NULL, OPTION_PROGRESS },
    { "pulsate", no_argument, NULL, OPTION_PULSATE },
    { "text", required_argument, NULL, OPTION_TEXT },
    { "title", required_argument, NULL, OPTION_TITLE },
    { "verbose", no_argument, NULL, OPTION_VERBOSE },
    { "version", no_argument, NULL, OPTION_VERSION },
    { "warning", no_argument, NULL, OPTION_WARNING },
    { "width", required_argument, NULL, OPTION_WIDTH },
    { NULL, 0, NULL, 0 }
};

int
main (int argc,
      char **argv)
{
    int ret = 255;
    char *endptr;
    int opt;

    unblock_signals_single_threaded();

    while ((opt = getopt_long(argc, argv, "v", long_options, NULL)) != -1) {
        switch (opt) {
            case OPTION_HELP:
                usage(EXIT_SUCCESS);
                break;    /* not reached */

            case OPTION_AUTO_CLOSE:
                opt_auto_close = true;
                break;

            case OPTION_CHECK_FEATURES:
                if (check_features(optarg) == 0) {
                    ret = EXIT_SUCCESS;
                }
                goto out;

            case OPTION_ERROR:
                if (set_mode(do_error) < 0) {
                    goto out;
                }
                break;

            case OPTION_HEIGHT:
                opt_height = strtol(optarg, &endptr, 10);
                if (endptr == NULL
                    || *endptr != '\0'
                    || optarg == endptr
                    || opt_height < 0) {
                    SDL_SetError("Invalid height \"%s\"", optarg);
                    goto out;
                }
                break;

            case OPTION_INFO:
                if (set_mode(do_info) < 0) {
                    goto out;
                }
                break;

            case OPTION_NO_CANCEL:
                opt_cancel = false;
                break;

            case OPTION_NO_WRAP:
                opt_wrap = false;
                break;

            case OPTION_PERCENTAGE:
                opt_percentage = strtod(optarg, &endptr);
                if (endptr == NULL
                    || *endptr != '\0'
                    || optarg == endptr
                    || opt_percentage < 0.0
                    || opt_percentage > 100.0) {
                    SDL_SetError("Invalid percentage \"%s\"", optarg);
                    goto out;
                }
                break;

            case OPTION_PROGRESS:
                if (set_mode(do_progress) < 0) {
                    goto out;
                }
                break;

            case OPTION_PULSATE:
                opt_pulsate = true;
                break;

            case OPTION_TEXT:
                opt_text = optarg;
                break;

            case OPTION_TITLE:
                opt_title = optarg;
                break;

            case OPTION_VERBOSE:
                if (opt_verbosity <= 3) {
                    opt_verbosity++;
                }
                break;

            case OPTION_VERSION:
                printf("%s:\n", THIS_PROGRAM);
                printf(" Package: steam-runtime-tools\n");
                printf(" VERSION: %s\n", VERSION);
                ret = EXIT_SUCCESS;
                goto out;

            case OPTION_WARNING:
                if (set_mode(do_warning) < 0) {
                    goto out;
                }
                break;

            case OPTION_WIDTH:
                opt_width = strtol(optarg, &endptr, 10);
                if (endptr == NULL
                    || *endptr != '\0'
                    || optarg == endptr
                    || opt_width < 0) {
                    SDL_SetError("Invalid width \"%s\"", optarg);
                    goto out;
                }
                break;

            case '?':
            default:
                usage(255);
                break;    /* not reached */
        }
    }

    if (opt_verbosity < 2 && getenv_bool("STEAM_RUNTIME_VERBOSE", false)) {
        opt_verbosity = 2;
    }

    /* The protocol used to tell Gamescope to count this window as part
     * of Steam only works under X11 */
    /* (This is currently not reached in practice because --check-features
     * bails out early under Gamescope) */
    if (strcmp0(getenv("XDG_CURRENT_DESKTOP"), "gamescope") == 0) {
        info("Forcing X11 video driver for Gamescope session");
        setenv("SDL_VIDEODRIVER", "x11", 1);
    }

    switch (opt_verbosity) {
        case 3:
            SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_VERBOSE);
            break;
        case 2:
            SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_DEBUG);
            break;
        case 1:
            SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);
            break;
        default:
            SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_WARN);
    }

    if (opt_mode == NULL) {
        SDL_SetError("A mode argument is required");
        goto out;
    }

    if (opt_mode() == 0) {
        ret = EXIT_SUCCESS;
    }

out:
    if (ret != EXIT_SUCCESS) {
        warnx("%s", SDL_GetError());
    }

    global_shutdown_ttf();
    global_shutdown_sdl();
    return ret;
}
