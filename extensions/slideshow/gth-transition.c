/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  GThumb
 *
 *  Copyright (C) 2009 Free Software Foundation, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include "gth-transition.h"


/* Properties */
enum {
        PROP_0,
        PROP_ID,
        PROP_DISPLAY_NAME,
        PROP_FRAME_FUNC
};


struct _GthTransitionPrivate {
	char      *id;
	char      *display_name;
	FrameFunc  frame_func;
};


static gpointer parent_class = NULL;


static void
gth_transition_init (GthTransition *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GTH_TYPE_TRANSITION, GthTransitionPrivate);
	self->priv->id = g_strdup ("");
	self->priv->display_name = g_strdup ("");
	self->priv->frame_func = NULL;
}


static void
gth_transition_finalize (GObject *object)
{
	GthTransition *self = GTH_TRANSITION (object);

	g_free (self->priv->id);
	g_free (self->priv->display_name);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static void
gth_transition_set_property (GObject      *object,
			     guint         property_id,
			     const GValue *value,
			     GParamSpec   *pspec)
{
	GthTransition *self = GTH_TRANSITION (object);

	switch (property_id) {
	case PROP_ID:
		g_free (self->priv->id);
		self->priv->id = g_value_dup_string (value);
		if (self->priv->id == NULL)
			self->priv->id = g_strdup ("");
		break;

	case PROP_DISPLAY_NAME:
		g_free (self->priv->display_name);
		self->priv->display_name = g_value_dup_string (value);
		if (self->priv->display_name == NULL)
			self->priv->display_name = g_strdup ("");
		break;

	case PROP_FRAME_FUNC:
		self->priv->frame_func = g_value_get_pointer (value);
		break;

	default:
		break;
	}
}


static void
gth_transition_get_property (GObject    *object,
			     guint       property_id,
			     GValue     *value,
			     GParamSpec *pspec)
{
	GthTransition *self = GTH_TRANSITION (object);

	switch (property_id) {
	case PROP_ID:
		g_value_set_string (value, self->priv->id);
		break;

	case PROP_DISPLAY_NAME:
		g_value_set_string (value, self->priv->display_name);
		break;

	case PROP_FRAME_FUNC:
		g_value_set_pointer (value, self->priv->frame_func);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gth_transition_class_init (GthTransitionClass *klass)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (klass);
	g_type_class_add_private (klass, sizeof (GthTransitionPrivate));

	object_class = G_OBJECT_CLASS (klass);
	object_class->get_property = gth_transition_get_property;
	object_class->set_property = gth_transition_set_property;
	object_class->finalize = gth_transition_finalize;

	g_object_class_install_property (object_class,
					 PROP_ID,
					 g_param_spec_string ("id",
                                                              "ID",
                                                              "The object id",
                                                              NULL,
                                                              G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_DISPLAY_NAME,
					 g_param_spec_string ("display-name",
                                                              "Display name",
                                                              "The user visible name",
                                                              NULL,
                                                              G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_FRAME_FUNC,
					 g_param_spec_pointer ("frame-func",
							       "Frame Function",
							       "The fuction used to set the current frame",
							       G_PARAM_READWRITE));
}


GType
gth_transition_get_type (void)
{
	static GType type = 0;

	if (! type) {
		GTypeInfo type_info = {
			sizeof (GthTransitionClass),
			NULL,
			NULL,
			(GClassInitFunc) gth_transition_class_init,
			NULL,
			NULL,
			sizeof (GthTransition),
			0,
			(GInstanceInitFunc) gth_transition_init
		};

		type = g_type_register_static (G_TYPE_OBJECT,
					       "GthTransition",
					       &type_info,
					       0);
	}

	return type;
}


const char *
gth_transition_get_id (GthTransition *self)
{
	return self->priv->id;
}


const char *
gth_transition_get_display_name (GthTransition *self)
{
	return self->priv->display_name;
}


void
gth_transition_frame (GthTransition *self,
		      GthSlideshow  *slideshow,
		      int            msecs)
{
	if (self->priv->frame_func != NULL)
		self->priv->frame_func (slideshow, msecs);
}
