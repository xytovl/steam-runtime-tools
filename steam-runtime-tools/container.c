/*
 * Copyright © 2021 Collabora Ltd.
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

#include "steam-runtime-tools/container.h"

#include "steam-runtime-tools/glib-backports-internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include "steam-runtime-tools/bwrap-internal.h"
#include "steam-runtime-tools/container-internal.h"
#include "steam-runtime-tools/enums.h"
#include "steam-runtime-tools/os-internal.h"
#include "steam-runtime-tools/resolve-in-sysroot-internal.h"
#include "steam-runtime-tools/utils.h"
#include "steam-runtime-tools/utils-internal.h"

#include "../pressure-vessel/flatpak-portal.h"

/**
 * SECTION:container
 * @title: Container info
 * @short_description: Information about the eventual container that is currently in use
 * @include: steam-runtime-tools/steam-runtime-tools.h
 */

struct _SrtContainerInfo
{
  /*< private >*/
  GObject parent;
  gchar *bwrap_messages;
  gchar *bwrap_path;
  gchar *flatpak_version;
  gchar *host_directory;
  SrtOsInfo *host_os_info;
  SrtContainerType type;
  SrtFlatpakIssues flatpak_issues;
  SrtBwrapIssues bwrap_issues;
};

struct _SrtContainerInfoClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum {
  PROP_0,
  PROP_BWRAP_ISSUES,
  PROP_BWRAP_MESSAGES,
  PROP_BWRAP_PATH,
  PROP_FLATPAK_ISSUES,
  PROP_FLATPAK_VERSION,
  PROP_HOST_DIRECTORY,
  PROP_HOST_OS_INFO,
  PROP_TYPE,
  N_PROPERTIES
};

G_DEFINE_TYPE (SrtContainerInfo, srt_container_info, G_TYPE_OBJECT)

static void
srt_container_info_init (SrtContainerInfo *self)
{
  self->bwrap_issues = SRT_BWRAP_ISSUES_UNKNOWN;
  self->flatpak_issues = SRT_FLATPAK_ISSUES_UNKNOWN;
}

static void
srt_container_info_get_property (GObject *object,
                                 guint prop_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
  SrtContainerInfo *self = SRT_CONTAINER_INFO (object);

  switch (prop_id)
    {
      case PROP_BWRAP_ISSUES:
        g_value_set_flags (value, srt_container_info_get_bwrap_issues (self));
        break;

      case PROP_BWRAP_PATH:
        g_value_set_string (value, self->bwrap_path);
        break;

      case PROP_BWRAP_MESSAGES:
        g_value_set_string (value, self->bwrap_messages);
        break;

      case PROP_FLATPAK_ISSUES:
        g_value_set_flags (value, srt_container_info_get_flatpak_issues (self));
        break;

      case PROP_FLATPAK_VERSION:
        g_value_set_string (value, self->flatpak_version);
        break;

      case PROP_HOST_DIRECTORY:
        g_value_set_string (value, self->host_directory);
        break;

      case PROP_HOST_OS_INFO:
        g_value_set_object (value, self->host_os_info);
        break;

      case PROP_TYPE:
        g_value_set_enum (value, self->type);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_container_info_set_property (GObject *object,
                                 guint prop_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
  SrtContainerInfo *self = SRT_CONTAINER_INFO (object);

  switch (prop_id)
    {
      case PROP_BWRAP_ISSUES:
        self->bwrap_issues = g_value_get_flags (value);
        break;

      case PROP_FLATPAK_ISSUES:
        self->flatpak_issues = g_value_get_flags (value);
        break;

      case PROP_BWRAP_MESSAGES:
        /* Construct-only */
        g_return_if_fail (self->bwrap_messages == NULL);
        self->bwrap_messages = g_value_dup_string (value);
        break;

      case PROP_BWRAP_PATH:
        /* Construct-only */
        g_return_if_fail (self->bwrap_path == NULL);
        self->bwrap_path = g_value_dup_string (value);
        break;

      case PROP_FLATPAK_VERSION:
        /* Construct-only */
        g_return_if_fail (self->flatpak_version == NULL);
        self->flatpak_version = g_value_dup_string (value);
        break;

      case PROP_HOST_DIRECTORY:
        /* Construct-only */
        g_return_if_fail (self->host_directory == NULL);
        self->host_directory = g_value_dup_string (value);
        break;

      case PROP_HOST_OS_INFO:
        /* Construct-only */
        g_return_if_fail (self->host_os_info == NULL);
        self->host_os_info = g_value_dup_object (value);
        break;

      case PROP_TYPE:
        /* Construct-only */
        g_return_if_fail (self->type == 0);
        self->type = g_value_get_enum (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_container_info_dispose (GObject *object)
{
  SrtContainerInfo *self = SRT_CONTAINER_INFO (object);

  g_clear_object (&self->host_os_info);

  G_OBJECT_CLASS (srt_container_info_parent_class)->dispose (object);
}

static void
srt_container_info_finalize (GObject *object)
{
  SrtContainerInfo *self = SRT_CONTAINER_INFO (object);

  g_free (self->bwrap_messages);
  g_free (self->bwrap_path);
  g_free (self->flatpak_version);
  g_free (self->host_directory);

  G_OBJECT_CLASS (srt_container_info_parent_class)->finalize (object);
}

static GParamSpec *properties[N_PROPERTIES] = { NULL };

static void
srt_container_info_class_init (SrtContainerInfoClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_container_info_get_property;
  object_class->set_property = srt_container_info_set_property;
  object_class->dispose = srt_container_info_dispose;
  object_class->finalize = srt_container_info_finalize;

  properties[PROP_BWRAP_ISSUES] =
    g_param_spec_flags ("bwrap-issues", "Bubblewrap issues",
                        "Any Bubblewrap-related problems that have been detected",
                         SRT_TYPE_BWRAP_ISSUES, SRT_BWRAP_ISSUES_UNKNOWN,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_BWRAP_MESSAGES] =
    g_param_spec_string ("bwrap-messages", "Bubblewrap messages",
                         "Diagnostic messages generated by Bubblewrap",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_BWRAP_PATH] =
    g_param_spec_string ("bwrap-path", "Bubblewrap path",
                         "Path to bwrap(1) executable",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_FLATPAK_ISSUES] =
    g_param_spec_flags ("flatpak-issues", "Flatpak issues",
                        "Any Flatpak-related problems that have been detected",
                         SRT_TYPE_FLATPAK_ISSUES, SRT_FLATPAK_ISSUES_UNKNOWN,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_FLATPAK_VERSION] =
    g_param_spec_string ("flatpak-version", "Flatpak version",
                         "Which Flatpak version, if any, is in use",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_HOST_DIRECTORY] =
    g_param_spec_string ("host-directory", "Host directory",
                         "Absolute path where important files from the host "
                         "system can be found",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_HOST_OS_INFO] =
    g_param_spec_object ("host-os-info", "Host OS information",
                         "Information about the OS of the host-directory if available",
                         SRT_TYPE_OS_INFO,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_TYPE] =
    g_param_spec_enum ("type", "Container type",
                       "Which container type is currently in use",
                       SRT_TYPE_CONTAINER_TYPE, SRT_CONTAINER_TYPE_UNKNOWN,
                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

typedef struct
{
  SrtContainerType type;
  const char *name;
} ContainerTypeName;

static const ContainerTypeName container_types[] =
{
  { SRT_CONTAINER_TYPE_DOCKER, "docker" },
  { SRT_CONTAINER_TYPE_FLATPAK, "flatpak" },
  { SRT_CONTAINER_TYPE_PODMAN, "podman" },
  { SRT_CONTAINER_TYPE_PRESSURE_VESSEL, "pressure-vessel" },
};

static SrtContainerType
container_type_from_name (const char *name)
{
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (container_types); i++)
    {
      const ContainerTypeName *entry = &container_types[i];

      if (strcmp (entry->name, name) == 0)
        return entry->type;
    }

  return SRT_CONTAINER_TYPE_UNKNOWN;
}

/**
 * _srt_check_container:
 * @sysroot: (not nullable): System root, often `/`
 *
 * Gather and return information about the container that is currently in use.
 *
 * Returns: (transfer full): A new #SrtContainerInfo object.
 *  Free with g_object_unref().
 */
SrtContainerInfo *
_srt_check_container (SrtSysroot *sysroot)
{
  g_autoptr(SrtSysroot) host_root = NULL;
  g_autoptr(SrtOsInfo) host_os_info = NULL;
  g_autofree gchar *contents = NULL;
  g_autofree gchar *run_host_path = NULL;
  g_autofree gchar *flatpak_version = NULL;
  SrtContainerType type = SRT_CONTAINER_TYPE_UNKNOWN;
  glnx_autofd int run_host_fd = -1;

  g_return_val_if_fail (SRT_IS_SYSROOT (sysroot), NULL);

  g_debug ("Finding container info in sysroot %s...", sysroot->path);

  run_host_fd = _srt_sysroot_open (sysroot, "/run/host",
                                   SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY,
                                   &run_host_path, NULL);

  g_debug ("/run/host resolved to %s", run_host_path ?: "(null)");

  /* Toolbx 0.0.99.3 makes /run/host a symlink to .. on the host system,
   * meaning the resolved path relative to the sysroot is ".".
   * We don't want that to be interpreted as being a container. */
  if (run_host_path != NULL && !g_str_equal (run_host_path, "."))
    host_root = _srt_sysroot_new_take (g_build_filename (sysroot->path,
                                                         run_host_path,
                                                         NULL),
                                       g_steal_fd (&run_host_fd));

  if (host_root != NULL
      && _srt_sysroot_load (sysroot, "/run/host/container-manager",
                            SRT_RESOLVE_FLAGS_NONE,
                            NULL, &contents, NULL, NULL))
    {
      g_strchomp (contents);
      type = container_type_from_name (contents);
      g_debug ("Type %d based on /run/host/container-manager", type);
      goto out;
    }

  if (_srt_sysroot_load (sysroot, "/run/systemd/container",
                         SRT_RESOLVE_FLAGS_NONE,
                         NULL, &contents, NULL, NULL))
    {
      g_strchomp (contents);
      type = container_type_from_name (contents);
      g_debug ("Type %d based on /run/systemd/container", type);
      goto out;
    }

  if (_srt_sysroot_test (sysroot, "/.flatpak-info",
                         SRT_RESOLVE_FLAGS_MUST_BE_REGULAR, NULL))
    {
      type = SRT_CONTAINER_TYPE_FLATPAK;
      g_debug ("Flatpak based on /.flatpak-info");
      goto out;
    }

  if (_srt_sysroot_test (sysroot, "/run/pressure-vessel",
                         SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY, NULL))
    {
      type = SRT_CONTAINER_TYPE_PRESSURE_VESSEL;
      g_debug ("pressure-vessel based on /run/pressure-vessel");
      goto out;
    }

  if (_srt_sysroot_test (sysroot, "/.dockerenv", SRT_RESOLVE_FLAGS_NONE, NULL))
    {
      type = SRT_CONTAINER_TYPE_DOCKER;
      g_debug ("Docker based on /.dockerenv");
      goto out;
    }

  if (_srt_sysroot_test (sysroot, "/run/.containerenv",
                         SRT_RESOLVE_FLAGS_NONE, NULL))
    {
      type = SRT_CONTAINER_TYPE_PODMAN;
      g_debug ("Podman based on /run/.containerenv");
      goto out;
    }

  /* The canonical way to detect Snap is to look for $SNAP, but it's
   * plausible that someone sets that variable for an unrelated reason,
   * so check for more than one variable. This is the same thing
   * WebKitGTK does. */
  if (g_getenv("SNAP") != NULL
      && g_getenv("SNAP_NAME") != NULL
      && g_getenv("SNAP_REVISION") != NULL)
    {
      type = SRT_CONTAINER_TYPE_SNAP;
      g_debug ("Snap based on $SNAP, $SNAP_NAME, $SNAP_REVISION");
      /* The way Snap works means that most of the host filesystem is
       * available in the root directory; but we're not allowed to access
       * it, so it wouldn't be useful to set host_root to "/". */
      goto out;
    }

  if (_srt_sysroot_load (sysroot, "/proc/1/cgroup",
                         SRT_RESOLVE_FLAGS_NONE,
                         NULL, &contents, NULL, NULL))
    {
      if (strstr (contents, "/docker/") != NULL)
        type = SRT_CONTAINER_TYPE_DOCKER;

      if (type != SRT_CONTAINER_TYPE_UNKNOWN)
        {
          g_debug ("Type %d based on /proc/1/cgroup", type);
          goto out;
        }

      g_clear_pointer (&contents, g_free);
    }

  if (host_root != NULL)
    {
      g_debug ("Unknown container technology based on /run/host");
      type = SRT_CONTAINER_TYPE_UNKNOWN;
      goto out;
    }

  /* We haven't found any particular evidence of being in a container */
  g_debug ("Probably not a container");
  type = SRT_CONTAINER_TYPE_NONE;

out:
  if (type == SRT_CONTAINER_TYPE_FLATPAK)
    {
      g_autoptr(GKeyFile) info = NULL;
      g_autoptr(GError) local_error = NULL;
      g_autofree gchar *flatpak_info_content = NULL;
      gsize len;

      if (_srt_sysroot_load (sysroot, "/.flatpak-info",
                             SRT_RESOLVE_FLAGS_NONE,
                             NULL, &flatpak_info_content, &len, NULL))
        {
          info = g_key_file_new ();

          if (!g_key_file_load_from_data (info, flatpak_info_content, len,
                                          G_KEY_FILE_NONE, &local_error))
            g_debug ("Unable to load Flatpak instance info: %s", local_error->message);

          flatpak_version = g_key_file_get_string (info,
                                                   FLATPAK_METADATA_GROUP_INSTANCE,
                                                   FLATPAK_METADATA_KEY_FLATPAK_VERSION,
                                                   NULL);
        }

      /* We don't check for Flatpak issues here, because that's more
       * time-consuming and not always needed.
       * Call _srt_container_info_check_flatpak() to add those. */
    }

  if (host_root != NULL)
    host_os_info = _srt_os_info_new_from_sysroot (host_root);

  return _srt_container_info_new (type,
                                  SRT_BWRAP_ISSUES_UNKNOWN, NULL, NULL,
                                  SRT_FLATPAK_ISSUES_UNKNOWN,
                                  flatpak_version,
                                  host_root != NULL ? host_root->path : NULL,
                                  host_os_info);
}

/**
 * srt_container_info_get_container_type:
 * @self: A SrtContainerInfo object
 *
 * If the program appears to be running in a container, return what sort
 * of container it is.
 *
 * Implementation of srt_system_info_get_container_type().
 *
 * Returns: A recognised container type, or %SRT_CONTAINER_TYPE_NONE
 *  if a container cannot be detected, or %SRT_CONTAINER_TYPE_UNKNOWN
 *  if unsure.
 */
SrtContainerType
srt_container_info_get_container_type (SrtContainerInfo *self)
{
  g_return_val_if_fail (SRT_IS_CONTAINER_INFO (self), SRT_CONTAINER_TYPE_UNKNOWN);
  return self->type;
}

/**
 * srt_container_info_get_container_host_directory:
 * @self: A SrtContainerInfo object
 *
 * If the program appears to be running in a container, return the
 * directory where host files can be found. For example, if this function
 * returns `/run/host`, it might be possible to load the host system's
 * `/usr/lib/os-release` by reading `/run/host/usr/lib/os-release`.
 *
 * The returned directory is usually not complete. For example,
 * in a Flatpak app, `/run/host` will sometimes contain the host system's
 * `/etc` and `/usr`, but only if suitable permissions flags are set.
 *
 * Implementation of srt_system_info_dup_container_host_directory().
 *
 * Returns: (type filename) (nullable): A path from which at least some
 *  host-system files can be loaded, typically `/run/host`, or %NULL if
 *  unknown or unavailable.
 */
const gchar *
srt_container_info_get_container_host_directory (SrtContainerInfo *self)
{
  g_return_val_if_fail (SRT_IS_CONTAINER_INFO (self), NULL);
  return self->host_directory;
}

/**
 * srt_container_info_get_container_host_os_info:
 * @self: A SrtContainerInfo object
 *
 * If the program appears to be running in a container, return
 * information about the host's operating system if possible.
 *
 * Returns: (transfer none) (nullable): Information about the host
 *  operating system, or %NULL if unknown or unavailable.
 */
SrtOsInfo *
srt_container_info_get_container_host_os_info (SrtContainerInfo *self)
{
  g_return_val_if_fail (SRT_IS_CONTAINER_INFO (self), NULL);
  return self->host_os_info;
}

/**
 * srt_container_info_get_bwrap_issues:
 * @self: A SrtContainerInfo object
 *
 * Return any Bubblewrap-specific issues that have been detected.
 *
 * Returns: Zero or more flags.
 */
SrtBwrapIssues
srt_container_info_get_bwrap_issues (SrtContainerInfo *self)
{
  g_return_val_if_fail (SRT_IS_CONTAINER_INFO (self), SRT_BWRAP_ISSUES_UNKNOWN);

  /* We do not expect to be able to run bubblewrap in a Flatpak sandbox. */
  if (self->type == SRT_CONTAINER_TYPE_FLATPAK)
    return SRT_BWRAP_ISSUES_CANNOT_RUN | SRT_BWRAP_ISSUES_NOT_TESTED;

  return self->bwrap_issues;
}

/**
 * srt_container_info_get_bwrap_path:
 * @self: A SrtContainerInfo object
 *
 * Return the path to `bwrap(1)`.
 *
 * Returns: (type filename) (nullable): A filename, or %NULL if not found
 */
const char *
srt_container_info_get_bwrap_path (SrtContainerInfo *self)
{
  g_return_val_if_fail (SRT_IS_CONTAINER_INFO (self), NULL);

  return self->bwrap_path;
}

/**
 * srt_container_info_get_bwrap_messages:
 * @self: A SrtContainerInfo object
 *
 * Return unstructed diagnostic messages related to `bwrap(1)`.
 *
 * Returns: (nullable): Diagnostic messages or %NULL
 */
const char *
srt_container_info_get_bwrap_messages (SrtContainerInfo *self)
{
  g_return_val_if_fail (SRT_IS_CONTAINER_INFO (self), NULL);

  return self->bwrap_messages;
}

/**
 * srt_container_info_get_flatpak_issues:
 * @self: A SrtContainerInfo object
 *
 * If the program appears to be running in a container type
 * %SRT_CONTAINER_TYPE_FLATPAK, return any Flatpak-specific issues detected.
 * Otherwise return %SRT_FLATPAK_ISSUES_NONE.
 *
 * Returns: Zero or more flags.
 */
SrtFlatpakIssues
srt_container_info_get_flatpak_issues (SrtContainerInfo *self)
{
  g_return_val_if_fail (SRT_IS_CONTAINER_INFO (self), SRT_FLATPAK_ISSUES_UNKNOWN);

  if (self->type != SRT_CONTAINER_TYPE_FLATPAK)
    return SRT_FLATPAK_ISSUES_NONE;

  return self->flatpak_issues;
}

/**
 * srt_container_info_get_flatpak_version:
 * @self: A SrtContainerInfo object
 *
 * If the program appears to be running in a container type
 * %SRT_CONTAINER_TYPE_FLATPAK, return the Flatpak version.
 *
 * Returns: (type filename) (nullable): A filename, or %NULL if the container
 *  type is not %SRT_CONTAINER_TYPE_FLATPAK or if it was not able to identify
 *  the Flatpak version.
 */
const gchar *
srt_container_info_get_flatpak_version (SrtContainerInfo *self)
{
  g_return_val_if_fail (SRT_IS_CONTAINER_INFO (self), NULL);

  if (self->type != SRT_CONTAINER_TYPE_FLATPAK)
    return NULL;

  return self->flatpak_version;
}

#define SRT_FLATPAK_ISSUES_ANY_SUBSANDBOX \
   (SRT_FLATPAK_ISSUES_TOO_OLD \
   | SRT_FLATPAK_ISSUES_SUBSANDBOX_UNAVAILABLE \
   | SRT_FLATPAK_ISSUES_SUBSANDBOX_LIMITED_BY_SETUID_BWRAP \
   | SRT_FLATPAK_ISSUES_SUBSANDBOX_DID_NOT_INHERIT_DISPLAY)

/* Replace all arguments after [0]. */
static void
set_subsandbox_check_args (GPtrArray *argv,
                           gsize keep_first_n,
                           SrtFlatpakIssues issues_to_detect)
{
  g_assert (argv->len >= keep_first_n);
  g_ptr_array_set_size (argv, keep_first_n);
  g_ptr_array_add (argv, g_strdup ("--bus-name"));
  g_ptr_array_add (argv, g_strdup (FLATPAK_PORTAL_BUS_NAME));

  if (issues_to_detect & SRT_FLATPAK_ISSUES_TOO_OLD)
    {
      /* This option is supported by the same Flatpak versions that
       * also support --usr-path, but has the advantage that it doesn't
       * require us to build a /usr for it. */
      g_ptr_array_add (argv, g_strdup ("--app-path"));
      g_ptr_array_add (argv, g_strdup (""));
    }

  /* Using this option has the side-effect of checking that bwrap does not
   * need to be setuid root on this host OS. */
  if (issues_to_detect & SRT_FLATPAK_ISSUES_SUBSANDBOX_LIMITED_BY_SETUID_BWRAP)
    g_ptr_array_add (argv, g_strdup ("--share-pids"));

  g_ptr_array_add (argv, g_strdup ("--"));

  /* This checks for
   * https://github.com/ValveSoftware/steam-for-linux/issues/10554 */
  if (issues_to_detect & SRT_FLATPAK_ISSUES_SUBSANDBOX_DID_NOT_INHERIT_DISPLAY)
    {
      g_ptr_array_add (argv, g_strdup ("/bin/sh"));
      g_ptr_array_add (argv, g_strdup ("-euc"));
      /* See _srt_check_flatpak_stdout() below */
      g_ptr_array_add (argv, g_strdup ("echo \"${DISPLAY+DISPLAY_is_set}\""));
    }
  else
    {
      g_ptr_array_add (argv, g_strdup ("true"));
    }

  g_ptr_array_add (argv, NULL);
}

/*
 * @out: The stdout of the command produced by
 *  `set_subsandbox_check_args (..., ... | SUBSANDBOX_DID_NOT_INHERIT_DISPLAY)`
 *
 * Return issues that can be detected by screen-scraping standard output.
 *
 * Returns: Zero or more of SUBSANDBOX_OUTPUT_CORRUPTED and
 *  SUBSANDBOX_DID_NOT_INHERIT_DISPLAY
 */
static SrtFlatpakIssues
_srt_check_flatpak_stdout (const char *out)
{
  if (g_str_equal (out, "DISPLAY_is_set\n"))
    {
      g_debug ("Subsandbox ran successfully");
      return SRT_FLATPAK_ISSUES_NONE;
    }
  else if (g_str_equal (out, "\n"))
    {
      g_info ("flatpak-portal is not inheriting the DISPLAY environment "
              "variable: please see "
              "https://github.com/ValveSoftware/steam-for-linux/issues/10554");
      return SRT_FLATPAK_ISSUES_SUBSANDBOX_DID_NOT_INHERIT_DISPLAY;
    }
  else
    {
      g_info ("Unknown output from subsandbox");

      if (strstr (out, "DISPLAY_is_set") != NULL)
        return SRT_FLATPAK_ISSUES_SUBSANDBOX_OUTPUT_CORRUPTED;
      else
        return (SRT_FLATPAK_ISSUES_SUBSANDBOX_DID_NOT_INHERIT_DISPLAY
                | SRT_FLATPAK_ISSUES_SUBSANDBOX_OUTPUT_CORRUPTED);
    }
}

static SrtFlatpakIssues
_srt_check_flatpak_subsandbox (SrtSubprocessRunner *runner)
{
  static const SrtFlatpakIssues pass_fail_checks[] =
  {
    SRT_FLATPAK_ISSUES_TOO_OLD,
    SRT_FLATPAK_ISSUES_SUBSANDBOX_LIMITED_BY_SETUID_BWRAP,
  };
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GPtrArray) argv = NULL;
  g_autoptr(SrtCompletedSubprocess) completed = NULL;
  size_t initial_argc;
  size_t i;
  SrtFlatpakIssues ret;

  argv = _srt_subprocess_runner_get_helper (runner,
                                            NULL,
                                            "steam-runtime-launch-client",
                                            SRT_HELPER_FLAGS_IN_BIN_DIR,
                                            &local_error);

  if (argv == NULL)
    {
      g_info ("Unable to find steam-runtime-launch-client to check "
              "subsandbox functionality: %s",
              local_error->message);
      return SRT_FLATPAK_ISSUES_SUBSANDBOX_NOT_CHECKED;
    }

  initial_argc = argv->len;

  /* First try for the happy path: creating a subsandbox works.
   * If this is OK then we're good. */
  set_subsandbox_check_args (argv, initial_argc,
                             SRT_FLATPAK_ISSUES_ANY_SUBSANDBOX);
  completed = _srt_subprocess_runner_run_sync (runner,
                                               (SRT_HELPER_FLAGS_TIME_OUT
                                                | SRT_HELPER_FLAGS_SHELL_EXIT_STATUS),
                                               (const char * const *) argv->pdata,
                                               SRT_SUBPROCESS_OUTPUT_CAPTURE_DEBUG,
                                               SRT_SUBPROCESS_OUTPUT_CAPTURE_DEBUG,
                                               &local_error);

  if (completed == NULL)
    {
      g_info ("Unable to run \"%s\" to check subsandbox functionality: %s",
              (const char *) argv->pdata[0], local_error->message);
      return SRT_FLATPAK_ISSUES_SUBSANDBOX_NOT_CHECKED;
    }

  if (_srt_completed_subprocess_check (completed, &local_error))
    {
      const char *out = _srt_completed_subprocess_get_stdout (completed);

      return _srt_check_flatpak_stdout (out);
   }
  else if (_srt_completed_subprocess_timed_out (completed))
    {
      g_info ("Creating subsandbox timed out");
      /* Don't do more specific checks in this case, because they will
       * presumably be equally time-consuming */
      return SRT_FLATPAK_ISSUES_SUBSANDBOX_TIMED_OUT;
    }

  g_clear_object (&completed);

  /* If the happy path didn't work, be more careful, by testing individual
   * features. */
  g_info ("%s", local_error->message);
  g_clear_error (&local_error);
  ret = SRT_FLATPAK_ISSUES_NONE;

  /* This run does two things: it checks whether subsandboxes can
   * work at all (e.g. if D-Bus activation is broken, then this will fail),
   * and it checks whether DISPLAY is inherited. */
  set_subsandbox_check_args (argv, initial_argc,
                             SRT_FLATPAK_ISSUES_SUBSANDBOX_DID_NOT_INHERIT_DISPLAY);
  completed = _srt_subprocess_runner_run_sync (runner,
                                               (SRT_HELPER_FLAGS_TIME_OUT
                                                | SRT_HELPER_FLAGS_SHELL_EXIT_STATUS),
                                               (const char * const *) argv->pdata,
                                               SRT_SUBPROCESS_OUTPUT_CAPTURE_DEBUG,
                                               SRT_SUBPROCESS_OUTPUT_CAPTURE_DEBUG,
                                               NULL);

  if (completed != NULL
      && _srt_completed_subprocess_check (completed, NULL))
    {
      const char *out = _srt_completed_subprocess_get_stdout (completed);

      ret |= _srt_check_flatpak_stdout (out);
    }
  else if (completed != NULL
           && _srt_completed_subprocess_timed_out (completed))
    {
      g_info ("Creating subsandbox timed out");
      return SRT_FLATPAK_ISSUES_SUBSANDBOX_TIMED_OUT;
    }
  else
    {
      /* Don't do more checks in this case, because if the simplest and
       * most basic subsandbox doesn't work, neither will any others */
      return SRT_FLATPAK_ISSUES_SUBSANDBOX_UNAVAILABLE;
    }

  g_clear_object (&completed);

  /* Each subsequent run checks a single feature. If we get here, we know
   * that subsandboxes are possible, therefore it must be that single
   * feature that caused them not to work this time. */
  for (i = 0; i < G_N_ELEMENTS (pass_fail_checks); i++)
    {
      set_subsandbox_check_args (argv, initial_argc, pass_fail_checks[i]);
      completed = _srt_subprocess_runner_run_sync (runner,
                                                   (SRT_HELPER_FLAGS_TIME_OUT
                                                    | SRT_HELPER_FLAGS_SHELL_EXIT_STATUS),
                                                   (const char * const *) argv->pdata,
                                                   SRT_SUBPROCESS_OUTPUT_CAPTURE_DEBUG,
                                                   SRT_SUBPROCESS_OUTPUT_CAPTURE_DEBUG,
                                                   NULL);

      /* We don't expect completed to be NULL here, because we already
       * proved that running launch_client can succeed - so if it's
       * NULL here, that must be some sort of internal error. */
      if (completed == NULL)
        ret |= SRT_FLATPAK_ISSUES_UNKNOWN;
      else if (!_srt_completed_subprocess_check (completed, NULL))
        ret |= pass_fail_checks[i];

      g_clear_object (&completed);
    }

  /* If flatpak is too old to understand --share-pids, then we can't
   * tell whether it really has a setuid bwrap. Assume it doesn't. */
  if (ret & SRT_FLATPAK_ISSUES_TOO_OLD)
    ret &= ~SRT_FLATPAK_ISSUES_SUBSANDBOX_LIMITED_BY_SETUID_BWRAP;

  if (ret == SRT_FLATPAK_ISSUES_NONE)
    {
      g_warning ("Unable to determine which subsandbox feature caused failure");
      ret = SRT_FLATPAK_ISSUES_UNKNOWN;
    }

  return ret;
}

static void
_srt_container_info_check_flatpak (SrtContainerInfo *self,
                                   SrtSubprocessRunner *runner)
{
  self->flatpak_issues = SRT_FLATPAK_ISSUES_NONE;

  /* We use 1.12 as our public-facing description of the required version
   * of Flatpak, but 1.11.1 is the bare minimum */
  if (self->flatpak_version == NULL)
    self->flatpak_issues |= SRT_FLATPAK_ISSUES_UNKNOWN;
  else if (strverscmp (self->flatpak_version, "1.11.1") < 0)
    self->flatpak_issues |= SRT_FLATPAK_ISSUES_TOO_OLD;

  if (runner == NULL)
    self->flatpak_issues |= SRT_FLATPAK_ISSUES_SUBSANDBOX_NOT_CHECKED;
  else
    self->flatpak_issues |= _srt_check_flatpak_subsandbox (runner);
}

static void
_srt_container_info_check_bwrap (SrtContainerInfo *self,
                                 SrtSysroot *sysroot,
                                 SrtSubprocessRunner *runner)
{
  g_clear_pointer (&self->bwrap_messages, g_free);
  g_clear_pointer (&self->bwrap_path, g_free);

  if (runner == NULL)
    self->bwrap_issues = SRT_BWRAP_ISSUES_NOT_TESTED;
  else
    self->bwrap_issues = _srt_check_bwrap_issues (sysroot, runner,
                                                  &self->bwrap_path,
                                                  &self->bwrap_messages);
}

void
_srt_container_info_check_issues (SrtContainerInfo *self,
                                  SrtSysroot *sysroot,
                                  SrtSubprocessRunner *runner)
{
  switch (self->type)
    {
      case SRT_CONTAINER_TYPE_FLATPAK:
        _srt_container_info_check_flatpak (self, runner);
        break;

      case SRT_CONTAINER_TYPE_DOCKER:
      case SRT_CONTAINER_TYPE_PODMAN:
      case SRT_CONTAINER_TYPE_PRESSURE_VESSEL:
      case SRT_CONTAINER_TYPE_SNAP:
      case SRT_CONTAINER_TYPE_UNKNOWN:
      case SRT_CONTAINER_TYPE_NONE:
      default:
        _srt_container_info_check_bwrap (self, sysroot, runner);
        break;
    }
}
