/*
 * Copyright © 2014-2019 Red Hat, Inc
 * Copyright © 2017-2024 Collabora Ltd.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "steam-runtime-tools/glib-backports-internal.h"

typedef struct _PvWrapContext PvWrapContext;
typedef struct _PvWrapContextClass PvWrapContextClass;

struct _PvWrapContext
{
  GObject parent_instance;

  gchar **original_environ;
};

#define PV_TYPE_WRAP_CONTEXT \
  (pv_wrap_context_get_type ())
#define PV_WRAP_CONTEXT(o) \
  (G_TYPE_CHECK_INSTANCE_CAST ((o), PV_TYPE_WRAP_CONTEXT, PvWrapContext))
#define PV_IS_WRAP_CONTEXT(o) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((o), PV_TYPE_WRAP_CONTEXT))
#define PV_WRAP_CONTEXT_GET_CLASS(o) \
  (G_TYPE_INSTANCE_GET_CLASS ((o), PV_TYPE_WRAP_CONTEXT, PvWrapContextClass))
#define PV_WRAP_CONTEXT_CLASS(c) \
  (G_TYPE_CHECK_CLASS_CAST ((c), PV_TYPE_WRAP_CONTEXT, PvWrapContextClass))
#define PV_IS_WRAP_CONTEXT_CLASS(c) \
  (G_TYPE_CHECK_CLASS_TYPE ((c), PV_TYPE_WRAP_CONTEXT))

GType pv_wrap_context_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (PvWrapContext, g_object_unref)

PvWrapContext *pv_wrap_context_new (GError **error);
