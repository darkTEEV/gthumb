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
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gthumb.h>
#include "actions.h"


#define BROWSER_DATA_KEY "rename-series-browser-data"


static const char *vfs_ui_info =
"<ui>"
"  <menubar name='MenuBar'>"
"    <menu name='Edit' action='EditMenu'>"
"      <placeholder name='Folder_Actions'>"
"        <menuitem action='Edit_Rename'/>"
"      </placeholder>"
"    </menu>"
"  </menubar>"
"</ui>";


static GtkActionEntry action_entries[] = {
	{ "Edit_Rename", NULL,
	  N_("_Rename"), "F2",
	  N_("Rename the selected files"),
	  G_CALLBACK (gth_browser_activate_action_edit_rename) },
};


typedef struct {
	GtkActionGroup *action_group;
	guint           vfs_merge_id;
} BrowserData;


static void
browser_data_free (BrowserData *data)
{
	g_free (data);
}


static void
set_action_sensitive (BrowserData *data,
		      const char  *action_name,
		      gboolean     sensitive)
{
	GtkAction *action;

	action = gtk_action_group_get_action (data->action_group, action_name);
	g_object_set (action, "sensitive", sensitive, NULL);
}


void
rs__gth_browser_construct_cb (GthBrowser *browser)
{
	BrowserData *data;

	g_return_if_fail (GTH_IS_BROWSER (browser));

	data = g_new0 (BrowserData, 1);

	data->action_group = gtk_action_group_new ("Rename Actions");
	gtk_action_group_set_translation_domain (data->action_group, NULL);
	gtk_action_group_add_actions (data->action_group,
				      action_entries,
				      G_N_ELEMENTS (action_entries),
				      browser);
	gtk_ui_manager_insert_action_group (gth_browser_get_ui_manager (browser), data->action_group, 0);

	g_object_set_data_full (G_OBJECT (browser), BROWSER_DATA_KEY, data, (GDestroyNotify) browser_data_free);
}


static void
file_manager_update_ui (BrowserData *data,
			GthBrowser  *browser)
{
	if (GTH_IS_FILE_SOURCE_VFS (gth_browser_get_location_source (browser))) {
		if (data->vfs_merge_id == 0) {
			GError *local_error = NULL;

			data->vfs_merge_id = gtk_ui_manager_add_ui_from_string (gth_browser_get_ui_manager (browser), vfs_ui_info, -1, &local_error);
			if (data->vfs_merge_id == 0) {
				g_warning ("building ui failed: %s", local_error->message);
				g_error_free (local_error);
			}
		}
	}
	else if (data->vfs_merge_id != 0) {
			gtk_ui_manager_remove_ui (gth_browser_get_ui_manager (browser), data->vfs_merge_id);
			data->vfs_merge_id = 0;
	}
}


void
rs__gth_browser_set_current_page_cb (GthBrowser *browser)
{
	BrowserData *data;

	data = g_object_get_data (G_OBJECT (browser), BROWSER_DATA_KEY);
	file_manager_update_ui (data, browser);
}


void
rs__gth_browser_load_location_after_cb (GthBrowser   *browser,
					GthFileData  *location_data,
					const GError *error)
{
	BrowserData *data;

	if ((location_data == NULL) || (error != NULL))
		return;

	data = g_object_get_data (G_OBJECT (browser), BROWSER_DATA_KEY);
	file_manager_update_ui (data, browser);
}


void
rs__gth_browser_update_sensitivity_cb (GthBrowser *browser)
{
	BrowserData   *data;
	GthFileSource *file_source;
	int            n_selected;
	gboolean       sensitive;

	data = g_object_get_data (G_OBJECT (browser), BROWSER_DATA_KEY);
	g_return_if_fail (data != NULL);

	file_source = gth_browser_get_location_source (browser);
	n_selected = gth_file_selection_get_n_selected (GTH_FILE_SELECTION (gth_browser_get_file_list_view (browser)));

	sensitive = (n_selected > 0) && (file_source != NULL);
	set_action_sensitive (data, "Edit_Rename", sensitive);
}
