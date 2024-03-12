/*
 * Copyright © 2014-2019 Red Hat, Inc
 * Copyright © 2017-2024 Collabora Ltd.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "pressure-vessel/wrap-context.h"

enum {
  PROP_0,
  N_PROPERTIES
};

struct _PvWrapContextClass
{
  GObjectClass parent_class;
};

static GParamSpec *properties[N_PROPERTIES] = { NULL };

G_DEFINE_TYPE (PvWrapContext, pv_wrap_context, G_TYPE_OBJECT)

static void
pv_wrap_context_init (PvWrapContext *self)
{
  self->original_environ = g_get_environ ();
}

static void
pv_wrap_context_finalize (GObject *object)
{
  PvWrapContext *self = PV_WRAP_CONTEXT (object);

  g_strfreev (self->original_environ);

  G_OBJECT_CLASS (pv_wrap_context_parent_class)->finalize (object);
}

static void
pv_wrap_context_class_init (PvWrapContextClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->finalize = pv_wrap_context_finalize;
}
