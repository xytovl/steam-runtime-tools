/*
 * Taken from Flatpak
 * Last updated: Flatpak 1.15.10
 *
 * Copyright © 2014 Red Hat, Inc
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#ifndef __FLATPAK_UTILS_H__
#define __FLATPAK_UTILS_H__

#include <string.h>

#include "libglnx.h"
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include "flatpak-error.h"
#include "flatpak-glib-backports-private.h"

#define AUTOFS_SUPER_MAGIC 0x0187

#define FLATPAK_XA_CACHE_VERSION 2
/* version 1 added extra data download size */
/* version 2 added ot.ts timestamps (to new format) */

#define FLATPAK_XA_SUMMARY_VERSION 1
/* version 0/missing is standard ostree summary,
 * version 1 is compact format with inline cache and no deltas
 */

/* Thse are key names in the per-ref metadata in the summary */
#define OSTREE_COMMIT_TIMESTAMP "ostree.commit.timestamp"
#define OSTREE_COMMIT_TIMESTAMP2 "ot.ts" /* Shorter version of the above */

#define FLATPAK_SUMMARY_DIFF_HEADER "xadf"

/* https://bugzilla.gnome.org/show_bug.cgi?id=766370 */
#if !GLIB_CHECK_VERSION (2, 49, 3)
#define FLATPAK_VARIANT_BUILDER_INITIALIZER {{0, }}
#define FLATPAK_VARIANT_DICT_INITIALIZER {{0, }}
#else
#define FLATPAK_VARIANT_BUILDER_INITIALIZER {{{0, }}}
#define FLATPAK_VARIANT_DICT_INITIALIZER {{{0, }}}
#endif

/* https://github.com/GNOME/libglnx/pull/38
 * Note by using #define rather than wrapping via a static inline, we
 * don't have to re-define attributes like G_GNUC_PRINTF.
 */
#define flatpak_fail glnx_throw

gboolean flatpak_fail_error (GError     **error,
                             FlatpakError code,
                             const char  *fmt,
                             ...) G_GNUC_PRINTF (3, 4);

gint flatpak_strcmp0_ptr (gconstpointer a,
                          gconstpointer b);

/* Sometimes this is /var/run which is a symlink, causing weird issues when we pass
 * it as a path into the sandbox */
char * flatpak_get_real_xdg_runtime_dir (void);

gboolean  flatpak_has_path_prefix (const char *str,
                                   const char *prefix);

const char * flatpak_path_match_prefix (const char *pattern,
                                        const char *path);

const char * flatpak_get_arch (void);
const char ** flatpak_get_arches (void);
gboolean flatpak_is_linux32_arch (const char *arch);
const char *flatpak_get_compat_arch (const char *kernel_arch);
const char *flatpak_get_compat_arch_reverse (const char *compat_arch);

const char ** flatpak_get_gl_drivers (void);
gboolean flatpak_extension_matches_reason (const char *extension_id,
                                           const char *reason,
                                           gboolean    default_value);

const char * flatpak_get_bwrap (void);
gboolean flatpak_bwrap_is_unprivileged (void);

char **flatpak_strv_sort_by_length (const char * const *strv);
char **flatpak_strv_merge (char   **strv1,
                           char   **strv2);
char **flatpak_subpaths_merge (char **subpaths1,
                               char **subpaths2);

GBytes * flatpak_read_stream (GInputStream * in,
                              gboolean null_terminate,
                              GError      **error);

gboolean flatpak_bytes_save (GFile        *dest,
                             GBytes       *bytes,
                             GCancellable *cancellable,
                             GError      **error);

gboolean flatpak_variant_save (GFile        *dest,
                               GVariant     *variant,
                               GCancellable *cancellable,
                               GError      **error);

gboolean flatpak_remove_dangling_symlinks (GFile        *dir,
                                           GCancellable *cancellable,
                                           GError      **error);

gboolean flatpak_g_ptr_array_contains_string (GPtrArray  *array,
                                              const char *str);

/* Returns the first string in subset that is not in strv */
static inline const gchar *
g_strv_subset (const gchar * const *strv,
               const gchar * const *subset)
{
  int i;

  for (i = 0; subset[i]; i++)
    {
      const char *key;

      key = subset[i];
      if (!g_strv_contains (strv, key))
        return key;
    }

  return NULL;
}

static inline void
flatpak_auto_unlock_helper (GMutex **mutex)
{
  if (*mutex)
    g_mutex_unlock (*mutex);
}

static inline GMutex *
flatpak_auto_lock_helper (GMutex *mutex)
{
  if (mutex)
    g_mutex_lock (mutex);
  return mutex;
}

gboolean flatpak_switch_symlink_and_remove (const char *symlink_path,
                                            const char *target,
                                            GError    **error);

char *flatpak_keyfile_get_string_non_empty (GKeyFile *keyfile,
                                            const char *group,
                                            const char *key);

GBytes *flatpak_zlib_compress_bytes   (GBytes  *bytes,
                                       int      level,
                                       GError **error);
GBytes *flatpak_zlib_decompress_bytes (GBytes  *bytes,
                                       GError **error);

void flatpak_parse_extension_with_tag (const char *extension,
                                       char      **name,
                                       char      **tag);

gboolean flatpak_argument_needs_quoting (const char *arg);
char * flatpak_quote_argv (const char *argv[],
                           gssize      len);
gboolean flatpak_file_arg_has_suffix (const char *arg,
                                      const char *suffix);

const char *flatpak_file_get_path_cached (GFile *file);

GFile *flatpak_build_file_va (GFile  *base,
                              va_list args);
GFile *flatpak_build_file (GFile *base,
                           ...) G_GNUC_NULL_TERMINATED;

gboolean flatpak_openat_noatime (int           dfd,
                                 const char   *name,
                                 int          *ret_fd,
                                 GCancellable *cancellable,
                                 GError      **error);

typedef enum {
  FLATPAK_CP_FLAGS_NONE = 0,
  FLATPAK_CP_FLAGS_MERGE = 1 << 0,
  FLATPAK_CP_FLAGS_NO_CHOWN = 1 << 1,
  FLATPAK_CP_FLAGS_MOVE = 1 << 2,
} FlatpakCpFlags;

gboolean   flatpak_cp_a (GFile         *src,
                         GFile         *dest,
                         FlatpakCpFlags flags,
                         GCancellable  *cancellable,
                         GError       **error);

gboolean flatpak_mkdir_p (GFile        *dir,
                          GCancellable *cancellable,
                          GError      **error);

gboolean flatpak_rm_rf (GFile        *dir,
                        GCancellable *cancellable,
                        GError      **error);

gboolean flatpak_canonicalize_permissions (int         parent_dfd,
                                           const char *rel_path,
                                           int         uid,
                                           int         gid,
                                           GError    **error);

gboolean flatpak_file_rename (GFile        *from,
                              GFile        *to,
                              GCancellable *cancellable,
                              GError      **error);

gboolean flatpak_open_in_tmpdir_at (int             tmpdir_fd,
                                    int             mode,
                                    char           *tmpl,
                                    GOutputStream **out_stream,
                                    GCancellable   *cancellable,
                                    GError        **error);

gboolean flatpak_buffer_to_sealed_memfd_or_tmpfile (GLnxTmpfile *tmpf,
                                                    const char  *name,
                                                    const char  *str,
                                                    size_t       len,
                                                    GError     **error);

static inline void
flatpak_temp_dir_destroy (void *p)
{
  GFile *dir = p;

  if (dir)
    {
      flatpak_rm_rf (dir, NULL, NULL);
      g_object_unref (dir);
    }
}

typedef GFile FlatpakTempDir;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (FlatpakTempDir, flatpak_temp_dir_destroy)

typedef GMainContext GMainContextPopDefault;
static inline void
flatpak_main_context_pop_default_destroy (void *p)
{
  GMainContext *main_context = p;

  if (main_context)
    {
      /* Ensure we don't leave some cleanup callbacks unhandled as we will never iterate this context again. */
      while (g_main_context_pending (main_context))
        g_main_context_iteration (main_context, TRUE);

      g_main_context_pop_thread_default (main_context);
      g_main_context_unref (main_context);
    }
}

static inline GMainContextPopDefault *
flatpak_main_context_new_default (void)
{
  GMainContext *main_context = g_main_context_new ();

  g_main_context_push_thread_default (main_context);
  return main_context;
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GMainContextPopDefault, flatpak_main_context_pop_default_destroy)

#define AUTOLOCK(name) G_GNUC_UNUSED __attribute__((cleanup (flatpak_auto_unlock_helper))) GMutex * G_PASTE (auto_unlock, __LINE__) = flatpak_auto_lock_helper (&G_LOCK_NAME (name))

char * flatpak_filter_glob_to_regexp (const char *glob, gboolean runtime_only, GError **error);
gboolean flatpak_parse_filters (const char *data,
                                GRegex **allow_refs_out,
                                GRegex **deny_refs_out,
                                GError **error);
gboolean flatpak_filters_allow_ref (GRegex *allow_refs,
                                    GRegex *deny_refs,
                                    const char *ref);

gboolean flatpak_allocate_tmpdir (int           tmpdir_dfd,
                                  const char   *tmpdir_relpath,
                                  const char   *tmpdir_prefix,
                                  char        **tmpdir_name_out,
                                  int          *tmpdir_fd_out,
                                  GLnxLockFile *file_lock_out,
                                  gboolean     *reusing_dir_out,
                                  GCancellable *cancellable,
                                  GError      **error);

gboolean flatpak_check_required_version (const char *ref,
                                         GKeyFile   *metakey,
                                         GError    **error);

int flatpak_levenshtein_distance (const char *s,
                                  gssize      ls,
                                  const char *t,
                                  gssize      lt);

char *   flatpak_dconf_path_for_app_id (const char *app_id);
gboolean flatpak_dconf_path_is_similar (const char *path1,
                                        const char *path2);

static inline void
null_safe_g_ptr_array_unref (gpointer data)
{
  g_clear_pointer (&data, g_ptr_array_unref);
}

GStrv flatpak_parse_env_block (const char  *data,
                               gsize        length,
                               GError     **error);

int flatpak_envp_cmp (const void *p1,
                      const void *p2);

gboolean flatpak_str_is_integer (const char *s);

gboolean flatpak_uri_equal (const char *uri1,
                            const char *uri2);

typedef enum {
  FLATPAK_ESCAPE_DEFAULT        = 0,
  FLATPAK_ESCAPE_ALLOW_NEWLINES = 1 << 0,
  FLATPAK_ESCAPE_DO_NOT_QUOTE   = 1 << 1,
} FlatpakEscapeFlags;

char * flatpak_escape_string (const char        *s,
                              FlatpakEscapeFlags flags);

gboolean flatpak_validate_path_characters (const char *path,
                                           GError    **error);

gboolean running_under_sudo (void);

#define FLATPAK_MESSAGE_ID "c7b39b1e006b464599465e105b361485"

#endif /* __FLATPAK_UTILS_H__ */
