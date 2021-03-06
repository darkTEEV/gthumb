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
#include <string.h>
#include <gthumb.h>
#include "gth-script.h"
#include "gth-script-file.h"


#define SCRIPT_FORMAT "1.0"


enum {
        CHANGED,
        LAST_SIGNAL
};


struct _GthScriptFilePrivate
{
	gboolean  loaded;
	GList    *items;
};


static gpointer parent_class = NULL;
static guint gth_script_file_signals[LAST_SIGNAL] = { 0 };

static GType gth_script_file_get_type (void);


static void
gth_script_file_finalize (GObject *object)
{
	GthScriptFile *self;

	self = GTH_SCRIPT_FILE (object);

	_g_object_list_unref (self->priv->items);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static void
gth_script_file_class_init (GthScriptFileClass *klass)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (klass);
	g_type_class_add_private (klass, sizeof (GthScriptFilePrivate));

	object_class = (GObjectClass*) klass;
	object_class->finalize = gth_script_file_finalize;

	gth_script_file_signals[CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GthScriptFileClass, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
}


static void
gth_script_file_init (GthScriptFile *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GTH_TYPE_SCRIPT_FILE, GthScriptFilePrivate);
	self->priv->items = NULL;
	self->priv->loaded = FALSE;
}


static GType
gth_script_file_get_type (void)
{
        static GType type = 0;

        if (! type) {
                GTypeInfo type_info = {
			sizeof (GthScriptFileClass),
			NULL,
			NULL,
			(GClassInitFunc) gth_script_file_class_init,
			NULL,
			NULL,
			sizeof (GthScriptFile),
			0,
			(GInstanceInitFunc) gth_script_file_init
		};

		type = g_type_register_static (G_TYPE_OBJECT,
					       "GthScriptFile",
					       &type_info,
					       0);
	}

        return type;
}


GthScriptFile *
gth_script_file_get (void)
{
	static GthScriptFile *script_file = NULL;

	if (script_file == NULL)
		script_file = g_object_new (GTH_TYPE_SCRIPT_FILE, NULL);

  	return script_file;
}


static gboolean
gth_script_file_load_from_data (GthScriptFile  *self,
                                const char     *data,
                                gsize           length,
                                GError        **error)
{
	DomDocument *doc;
	gboolean     success;

	doc = dom_document_new ();
	success = dom_document_load (doc, data, length, error);
	if (success) {
		DomElement *scripts_node;
		DomElement *child;
		GList      *new_items = NULL;

		scripts_node = DOM_ELEMENT (doc)->first_child;
		if ((scripts_node != NULL) && (g_strcmp0 (scripts_node->tag_name, "scripts") == 0)) {
			for (child = scripts_node->first_child;
			     child != NULL;
			     child = child->next_sibling)
			{
				GthScript *script = NULL;

				if (strcmp (child->tag_name, "script") == 0) {
					script = gth_script_new ();
					dom_domizable_load_from_element (DOM_DOMIZABLE (script), child);
				}

				if (script == NULL)
					continue;

				new_items = g_list_prepend (new_items, script);
			}

			new_items = g_list_reverse (new_items);
			self->priv->items = g_list_concat (self->priv->items, new_items);
		}
	}
	g_object_unref (doc);

	return success;
}


static gboolean
gth_script_file_load_from_file (GthScriptFile  *self,
                                const char     *filename,
                                GError        **error)
{
	char     *buffer;
	gsize     len;
	GError   *read_error;
	gboolean  retval;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);

	read_error = NULL;
	g_file_get_contents (filename, &buffer, &len, &read_error);
	if (read_error != NULL) {
		g_propagate_error (error, read_error);
		return FALSE;
	}

	read_error = NULL;
	retval = gth_script_file_load_from_data (self,
						 buffer,
                                           	 len,
                                           	 &read_error);
  	if (read_error != NULL) {
		g_propagate_error (error, read_error);
		g_free (buffer);
		return FALSE;
	}

  	g_free (buffer);

	return retval;
}


static void
_gth_script_file_load_if_needed (GthScriptFile *self)
{
	char *default_script_file;

	if (self->priv->loaded)
		return;

	default_script_file = gth_user_dir_get_file (GTH_DIR_CONFIG, GTHUMB_DIR, "scripts.xml", NULL);
	gth_script_file_load_from_file (self, default_script_file, NULL);
	self->priv->loaded = TRUE;

	g_free (default_script_file);
}


static char *
gth_script_file_to_data (GthScriptFile  *self,
			 gsize          *len,
			 GError        **data_error)
{
	DomDocument *doc;
	DomElement  *root;
	char        *data;
	GList       *scan;

	doc = dom_document_new ();
	root = dom_document_create_element (doc, "scripts",
					    "version", SCRIPT_FORMAT,
					    NULL);
	dom_element_append_child (DOM_ELEMENT (doc), root);
	for (scan = self->priv->items; scan; scan = scan->next)
		dom_element_append_child (root, dom_domizable_create_element (DOM_DOMIZABLE (scan->data), doc));
	data = dom_document_dump (doc, len);

	g_object_unref (doc);

	return data;
}


static gboolean
gth_script_file_to_file (GthScriptFile  *self,
                         const char     *filename,
                         GError        **error)
{
	char     *data;
	GError   *data_error, *write_error;
	gsize     len;
	gboolean  retval;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);

	data_error = NULL;
	data = gth_script_file_to_data (self, &len, &data_error);
	if (data_error) {
		g_propagate_error (error, data_error);
		return FALSE;
	}

	write_error = NULL;
	g_file_set_contents (filename, data, len, &write_error);
	if (write_error) {
		g_propagate_error (error, write_error);
		retval = FALSE;
	}
	else
		retval = TRUE;

	g_free (data);

	return retval;
}


GthScript *
gth_script_file_get_script (GthScriptFile *self,
			    const char    *id)
{
	GList *scan;

	for (scan = self->priv->items; scan; scan = scan->next) {
		GthScript *script = scan->data;

		if (g_strcmp0 (gth_script_get_id (script), id) == 0)
			return script;
	}

	return NULL;
}


gboolean
gth_script_file_save (GthScriptFile  *self,
		      GError        **error)
{
	char     *default_script_file;
	gboolean  result;

	_gth_script_file_load_if_needed (self);

	default_script_file = gth_user_dir_get_file (GTH_DIR_CONFIG, GTHUMB_DIR, "scripts.xml", NULL);
	result = gth_script_file_to_file (self, default_script_file, error);
	if (result)
		g_signal_emit (G_OBJECT (self), gth_script_file_signals[CHANGED], 0);

	g_free (default_script_file);

	return result;
}


GList *
gth_script_file_get_scripts (GthScriptFile *self)
{
	GList *list;
	GList *scan;

	_gth_script_file_load_if_needed (self);

	list = NULL;
	for (scan = self->priv->items; scan; scan = scan->next)
		list = g_list_prepend (list, gth_duplicable_duplicate (GTH_DUPLICABLE (scan->data)));

	return g_list_reverse (list);
}


static int
find_by_id (gconstpointer a,
            gconstpointer b)
{
	GthScript *script = (GthScript *) a;
	GthScript *script_to_find = (GthScript *) b;

	return g_strcmp0 (gth_script_get_id (script), gth_script_get_id (script_to_find));
}


gboolean
gth_script_file_has_script (GthScriptFile *self,
			    GthScript     *script)
{
	return g_list_find_custom (self->priv->items, script, find_by_id) != NULL;
}


void
gth_script_file_add (GthScriptFile *self,
		     GthScript     *script)
{
	GList *link;

	_gth_script_file_load_if_needed (self);

	g_object_ref (script);

	link = g_list_find_custom (self->priv->items, script, find_by_id);
	if (link != NULL) {
		g_object_unref (link->data);
		link->data = script;
	}
	else
		self->priv->items = g_list_append (self->priv->items, script);
}


void
gth_script_file_remove (GthScriptFile *self,
			GthScript     *script)
{
	GList *link;

	_gth_script_file_load_if_needed (self);

	link = g_list_find_custom (self->priv->items, script, find_by_id);
	if (link == NULL)
		return;
	self->priv->items = g_list_remove_link (self->priv->items, link);

	_g_object_list_unref (link);
}


void
gth_script_file_clear (GthScriptFile *self)
{
	_g_object_list_unref (self->priv->items);
	self->priv->items = NULL;
}
