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
#include <gthumb.h>
#include "gth-slideshow.h"
#include "gth-transition.h"
#include "preferences.h"


void
gth_browser_activate_action_view_slideshow (GtkAction  *action,
					    GthBrowser *browser)
{
	GList        *items;
	GList        *file_list;
	GthProjector *projector;
	GtkWidget    *slideshow;
	GthFileData  *location;
	char         *transition_id;
	GList        *transitions = NULL;

	items = gth_file_selection_get_selected (GTH_FILE_SELECTION (gth_browser_get_file_list_view (browser)));
	if ((items == NULL) || (items->next == NULL))
		file_list = gth_file_store_get_visibles (GTH_FILE_STORE (gth_browser_get_file_store (browser)));
	else
		file_list = gth_file_list_get_files (GTH_FILE_LIST (gth_browser_get_file_list (browser)), items);

	projector = NULL;
#ifdef HAVE_CLUTTER
	if (ClutterInitResult == CLUTTER_INIT_SUCCESS)
		projector = &clutter_projector;
#endif /* HAVE_CLUTTER */
	if (projector == NULL)
		projector = &default_projector;

	slideshow = gth_slideshow_new (projector, browser, file_list);

	location = gth_browser_get_location_data (browser);
	if (g_file_info_get_attribute_boolean (location->info, "slideshow::personalize")) {
		gth_slideshow_set_delay (GTH_SLIDESHOW (slideshow), g_file_info_get_attribute_int32 (location->info, "slideshow::delay"));
		gth_slideshow_set_automatic (GTH_SLIDESHOW (slideshow), g_file_info_get_attribute_boolean (location->info, "slideshow::automatic"));
		gth_slideshow_set_wrap_around (GTH_SLIDESHOW (slideshow), g_file_info_get_attribute_boolean (location->info, "slideshow::wrap-around"));
		transition_id = g_strdup (g_file_info_get_attribute_string (location->info, "slideshow::transition"));
	}
	else {
		gth_slideshow_set_delay (GTH_SLIDESHOW (slideshow), (guint) (1000.0 * eel_gconf_get_float (PREF_SLIDESHOW_CHANGE_DELAY, 5.0)));
		gth_slideshow_set_automatic (GTH_SLIDESHOW (slideshow), eel_gconf_get_boolean (PREF_SLIDESHOW_AUTOMATIC, TRUE));
		gth_slideshow_set_wrap_around (GTH_SLIDESHOW (slideshow), eel_gconf_get_boolean (PREF_SLIDESHOW_WRAP_AROUND, FALSE));
		transition_id = eel_gconf_get_string (PREF_SLIDESHOW_TRANSITION, DEFAULT_TRANSITION);
	}

	if (g_file_info_get_attribute_status (location->info, "slideshow::playlist") == G_FILE_ATTRIBUTE_STATUS_SET)
		gth_slideshow_set_playlist (GTH_SLIDESHOW (slideshow),
					    g_file_info_get_attribute_stringv (location->info, "slideshow::playlist"));

	if (strcmp (transition_id, "random") == 0) {
		GList *scan;

		transitions = gth_main_get_registered_objects (GTH_TYPE_TRANSITION);
		for (scan = transitions; scan; scan = scan->next) {
			GthTransition *transition = scan->data;

			if (strcmp (gth_transition_get_id (transition), "none") == 0) {
				transitions = g_list_remove_link (transitions, scan);
				_g_object_list_unref (scan);
				break;
			}
		}
	}
	else {
		GthTransition *transition = gth_main_get_registered_object (GTH_TYPE_TRANSITION, transition_id);

		if (transition != NULL)
			transitions = g_list_append (NULL, transition);
		else
			transitions = NULL;
	}
	gth_slideshow_set_transitions (GTH_SLIDESHOW (slideshow), transitions);

	gtk_window_fullscreen (GTK_WINDOW (slideshow));
	/*gtk_window_set_default_size (GTK_WINDOW (slideshow), 700, 700);*/
	gtk_window_present (GTK_WINDOW (slideshow));

	_g_object_list_unref (transitions);
	g_free (transition_id);
	_g_object_list_unref (file_list);
	_gtk_tree_path_list_free (items);
}
