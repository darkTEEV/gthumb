/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  GThumb
 *
 *  Copyright (C) 2005 Free Software Foundation, Inc.
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

#include <stdlib.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "comments.h"
#include "dlg-file-utils.h"
#include "file-utils.h"
#include "gconf-utils.h"
#include "glib-utils.h"
#include "gtk-utils.h"
#include "gth-exif-utils.h"
#include "gth-fullscreen.h"
#include "gth-window.h"
#include "gth-window-utils.h"
#include "gthumb-preloader.h"
#include "gthumb-stock.h"
#include "image-viewer.h"
#include "preferences.h"

#include "icons/pixbufs.h"

#define HIDE_DELAY 1000
#define X_PADDING  12
#define Y_PADDING  6
#define MOTION_THRESHOLD 3
#define DEF_SLIDESHOW_DELAY 4
#define SECONDS 1000


struct _GthFullscreenPrivateData {
	GdkPixbuf       *image;
	char            *image_path;
	GList           *file_list;
	int              files;
	char            *catalog_path;
	GList           *current;
	gboolean         slideshow;
	GthDirectionType slideshow_direction;
	int              slideshow_delay;
	gboolean         slideshow_wrap_around;
	guint            slideshow_timeout;
	guint            slideshow_paused;

	gboolean         first_time_show;
	guint            mouse_hide_id;

	GThumbPreloader *preloader;
	char            *requested_path;

	GtkWidget       *viewer;
	GtkWidget       *toolbar_window;
	GtkWidget       *info_button;
	GtkWidget       *pause_button;
	GtkWidget       *prev_button;
	GtkWidget       *next_button;

	/* comment */

	gboolean        comment_visible;
	gboolean        image_data_visible;
	GdkPixmap      *buffer;
	GdkPixmap      *original_buffer;
	PangoRectangle  bounds;
};

static GthWindowClass *parent_class = NULL;


static void 
gth_fullscreen_finalize (GObject *object)
{
	GthFullscreen *fullscreen = GTH_FULLSCREEN (object);

	debug (DEBUG_INFO, "Gth::Fullscreen::Finalize");

	if (fullscreen->priv != NULL) {
		GthFullscreenPrivateData *priv = fullscreen->priv;

		if (priv->slideshow_timeout != 0) {
			g_source_remove (priv->slideshow_timeout);
			priv->slideshow_timeout = 0;
		}

		if (priv->toolbar_window != NULL) {
			gtk_widget_destroy (priv->toolbar_window);
			priv->toolbar_window = NULL;
		}

		if (priv->mouse_hide_id != 0)
			g_source_remove (priv->mouse_hide_id);

		if (priv->image != NULL) {
			g_object_unref (priv->image);
			priv->image = NULL;
		}

		g_free (priv->image_path);
		g_free (priv->requested_path);
		path_list_free (priv->file_list);
		g_free (priv->catalog_path);
		
		g_object_unref (priv->preloader);

		if (priv->buffer != NULL) {
			g_object_unref (priv->buffer);
			g_object_unref (priv->original_buffer);
			priv->buffer = NULL;
			priv->original_buffer = NULL;
		}

		/**/

		g_free (fullscreen->priv);
		fullscreen->priv = NULL;
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);
}


static void load_next_image (GthFullscreen *fullscreen);


static gboolean
slideshow_timeout_cb (gpointer data)
{
	GthFullscreen            *fullscreen = data;
	GthFullscreenPrivateData *priv = fullscreen->priv;

	if (priv->slideshow_timeout != 0) {
		g_source_remove (priv->slideshow_timeout);
		priv->slideshow_timeout = 0;
	}

	if (!priv->slideshow_paused)
		load_next_image (fullscreen);

	return FALSE;
}


static void
continue_slideshow (GthFullscreen *fullscreen)
{
	GthFullscreenPrivateData *priv = fullscreen->priv;

	if (priv->slideshow_timeout != 0) {
		g_source_remove (priv->slideshow_timeout);
		priv->slideshow_timeout = 0;
	}

	priv->slideshow_timeout = g_timeout_add (priv->slideshow_delay * SECONDS, 
						 slideshow_timeout_cb, 
						 fullscreen);
}


static void
preloader_requested_error_cb (GThumbPreloader *gploader,
			      GthFullscreen   *fullscreen)
{
	GthFullscreenPrivateData *priv = fullscreen->priv;

	image_viewer_set_void (IMAGE_VIEWER (priv->viewer));

	if (priv->slideshow)
		continue_slideshow (fullscreen);
}


static void
preloader_requested_done_cb (GThumbPreloader *gploader,
			     GthFullscreen   *fullscreen)
{
	GthFullscreenPrivateData *priv = fullscreen->priv;
	ImageLoader              *loader;

	loader = gthumb_preloader_get_loader (priv->preloader, priv->requested_path);
	if (loader != NULL) 
		image_viewer_load_from_image_loader (IMAGE_VIEWER (priv->viewer), loader);

	if (priv->slideshow)
		continue_slideshow (fullscreen);
}


static void
gth_fullscreen_init (GthFullscreen *fullscreen)
{
	GthFullscreenPrivateData *priv;

	priv = fullscreen->priv = g_new0 (GthFullscreenPrivateData, 1);
	priv->first_time_show = TRUE;
	priv->mouse_hide_id = 0;

	priv->preloader = gthumb_preloader_new ();
	g_signal_connect (G_OBJECT (priv->preloader),
			  "requested_done",
			  G_CALLBACK (preloader_requested_done_cb),
			  fullscreen);
	g_signal_connect (G_OBJECT (priv->preloader),
			  "requested_error",
			  G_CALLBACK (preloader_requested_error_cb),
			  fullscreen);
}


static gboolean
hide_mouse_pointer_cb (gpointer data)
{
	GthFullscreen            *fullscreen = data;
	GthFullscreenPrivateData *priv = fullscreen->priv;
	int                       x, y, w, h, px, py;

	gdk_window_get_pointer (priv->toolbar_window->window, &px, &py, 0);
	gdk_window_get_geometry (priv->toolbar_window->window, &x, &y, &w, &h, NULL);

	if ((px >= x) && (px <= x + w) && (py >= y) && (py <= y + h)) 
		return TRUE;

	gtk_widget_hide (priv->toolbar_window);

	image_viewer_hide_cursor (IMAGE_VIEWER (priv->viewer));
	priv->mouse_hide_id = 0;

	if (priv->image_data_visible)
		priv->comment_visible = TRUE;
	if (priv->comment_visible) 
		gtk_widget_queue_draw (fullscreen->priv->viewer);

        return FALSE;
}


static gboolean
motion_notify_event_cb (GtkWidget      *widget, 
			GdkEventMotion *event, 
			gpointer        data)
{
	GthFullscreen            *fullscreen = GTH_FULLSCREEN (widget);
	GthFullscreenPrivateData *priv = fullscreen->priv;
	static int                last_px = 0, last_py = 0;
	int                       px, py;

	gdk_window_get_pointer (widget->window, &px, &py, NULL);

	if (last_px == 0)
		last_px = px;
	if (last_py == 0)
		last_py = py;

	if ((abs (last_px - px) > MOTION_THRESHOLD) 
	    || (abs (last_py - py) > MOTION_THRESHOLD)) 
		if (! GTK_WIDGET_VISIBLE (priv->toolbar_window)) {
			gtk_widget_show (priv->toolbar_window);
			image_viewer_show_cursor (IMAGE_VIEWER (priv->viewer));
		}

	if (priv->mouse_hide_id != 0)
		g_source_remove (priv->mouse_hide_id);
	priv->mouse_hide_id = g_timeout_add (HIDE_DELAY,
					     hide_mouse_pointer_cb,
					     fullscreen);

	last_px = px;
	last_py = py;

	return FALSE;
}


static const char*
get_image_filename (GList *current)
{
	if (current == NULL)
		return NULL;
	return current->data;
}


static GList*
get_next_image (GthFullscreen *fullscreen)
{
	GthFullscreenPrivateData *priv = fullscreen->priv;

	if (priv->current == NULL)
		return NULL;

	if (priv->slideshow && (priv->slideshow_direction == GTH_DIRECTION_REVERSE))
		return priv->current->prev;
	else
		return priv->current->next;
}


static GList*
get_prev_image (GthFullscreen *fullscreen)
{
	GthFullscreenPrivateData *priv = fullscreen->priv;

	if (priv->current == NULL)
		return NULL;

	if (priv->slideshow && (priv->slideshow_direction == GTH_DIRECTION_REVERSE))
		return priv->current->next;
	else
		return priv->current->prev;
}


static void
update_sensitivity (GthFullscreen *fullscreen)
{
	GthFullscreenPrivateData *priv = fullscreen->priv;

	gtk_widget_set_sensitive (priv->prev_button, 
				  get_prev_image (fullscreen) != NULL);
	gtk_widget_set_sensitive (priv->next_button, 
				  get_next_image (fullscreen) != NULL);
}


static void
load_current_image (GthFullscreen *fullscreen)
{
	GthFullscreenPrivateData *priv = fullscreen->priv;
	gboolean from_pixbuf = TRUE;

	if (priv->current != NULL) {
		char *filename = (char*) priv->current->data;

		if ((priv->image != NULL) 
		    && (priv->image_path != NULL) 
		    && (strcmp (priv->image_path, filename) == 0))
			image_viewer_set_pixbuf (IMAGE_VIEWER (priv->viewer), priv->image);

		else {
			g_free (priv->requested_path);
			priv->requested_path = g_strdup (filename);
			gthumb_preloader_start (priv->preloader,
						priv->requested_path,
						get_image_filename (get_next_image (fullscreen)),
						get_image_filename (get_prev_image (fullscreen)),
						NULL);
			from_pixbuf = FALSE;
		}

	} else if (priv->image != NULL) 
		image_viewer_set_pixbuf (IMAGE_VIEWER (priv->viewer), priv->image);

	update_sensitivity (fullscreen);

	if (priv->slideshow && from_pixbuf)
		continue_slideshow (fullscreen);
}


static int
random_list_func (gconstpointer a,
		  gconstpointer b)
{
	return g_random_int_range (-1, 2); 
}


static void
load_first_or_last_image (GthFullscreen *fullscreen,
			  gboolean       first)
{
	GthFullscreenPrivateData *priv = fullscreen->priv;

	if (priv->file_list != NULL) {
		if (priv->slideshow) {
			if (priv->slideshow_direction == GTH_DIRECTION_RANDOM)
				priv->file_list = g_list_sort (priv->file_list, random_list_func);
			
			if (priv->slideshow_direction == GTH_DIRECTION_REVERSE) {
				if (first)
					priv->current = g_list_last (priv->file_list);
				else
					priv->current = priv->file_list;
			} else {
				if (first)
					priv->current = priv->file_list;
				else
					priv->current = g_list_last (priv->file_list);
			}

		} else if (priv->image_path != NULL)
			priv->current = g_list_find_custom (priv->file_list, 
							    priv->image_path,
							    (GCompareFunc) strcmp);
		
		if (priv->current == NULL)
			priv->current = priv->file_list;
	}

	load_current_image (fullscreen);
}


static void
load_first_image (GthFullscreen *fullscreen)
{
	load_first_or_last_image (fullscreen, TRUE);
}


static void
load_last_image (GthFullscreen *fullscreen)
{
	load_first_or_last_image (fullscreen, FALSE);
}


static void
load_next_image (GthFullscreen *fullscreen)
{
	GthFullscreenPrivateData *priv = fullscreen->priv;
	GList *next;

	next = get_next_image (fullscreen);
	if (next != NULL)
		priv->current = next;
	
	if ((next == NULL) && priv->slideshow && priv->slideshow_wrap_around)
		load_first_image (fullscreen);
	else
		load_current_image (fullscreen);
}


static void
load_prev_image (GthFullscreen *fullscreen)
{
	GthFullscreenPrivateData *priv = fullscreen->priv;
	GList *next;

	if (priv->slideshow && (priv->slideshow_direction == GTH_DIRECTION_REVERSE))
		next = get_next_image (fullscreen);
	else
		next = get_prev_image (fullscreen);

	if (next != NULL)
		priv->current = next;

	if ((next == NULL) && priv->slideshow  && priv->slideshow_wrap_around)
		load_last_image (fullscreen);
	else
		load_current_image (fullscreen);
}


static void
make_transparent (guchar *data, int i, guchar alpha)
{
	i *= 4;

	data[i++] = 0x00;
	data[i++] = 0x00;
	data[i++] = 0x00;
	data[i++] = alpha;
}


static void
render_background (GdkDrawable    *drawable,
		   PangoRectangle *bounds)
{
	GdkPixbuf *pixbuf;
	guchar    *data;
	int        rowstride;

	pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, 
				 bounds->width, bounds->height);
	gdk_pixbuf_fill (pixbuf, 0xF0F0F0D0);

	/**/

	data = gdk_pixbuf_get_pixels (pixbuf);
	rowstride = gdk_pixbuf_get_rowstride (pixbuf);

	/* first corner */
	make_transparent (data, 0, 0x00);
	make_transparent (data, 1, 0x33);
	make_transparent (data, bounds->width, 0x33);

	/* second corner */
	make_transparent (data, bounds->width - 2, 0x33);
	make_transparent (data, bounds->width - 1, 0x00);
	make_transparent (data, bounds->width * 2 - 1, 0x33);

	/* third corner */
	make_transparent (data, bounds->width * (bounds->height - 2), 0x33);
	make_transparent (data, bounds->width * (bounds->height - 1), 0x00);
	make_transparent (data, bounds->width * (bounds->height - 1) + 1, 0x33);

	/* forth corner */
	make_transparent (data, bounds->width * (bounds->height - 1) - 1, 0x33);
	make_transparent (data, bounds->width * bounds->height - 2, 0x33);
	make_transparent (data, bounds->width * bounds->height - 1, 0x00);

	/**/

	gdk_pixbuf_render_to_drawable_alpha (pixbuf,
					     drawable,
					     0, 0,
					     0, 0,
					     bounds->width, bounds->height,
					     GDK_PIXBUF_ALPHA_FULL, 
					     255,
					     GDK_RGB_DITHER_NONE, 0, 0);
	g_object_unref (pixbuf);
}


static void
render_frame (GdkDrawable    *drawable,
	      GdkGC          *gc,
	      PangoRectangle *bounds)
{
	GdkPoint p[9];
	
	p[0].x = 2;
	p[0].y = 0;
	p[1].x = bounds->width - 3;
	p[1].y = 0;
	p[2].x = bounds->width - 1;
	p[2].y = 2;
	p[3].x = bounds->width - 1;
	p[3].y = bounds->height - 3;
	p[4].x = bounds->width - 3;
	p[4].y = bounds->height - 1;
	p[5].x = 2;
	p[5].y = bounds->height - 1;
	p[6].x = 0;
	p[6].y = bounds->height - 3;
	p[7].x = 0;
	p[7].y = 2;
	p[8].x = 2;
	p[8].y = 0;
	
	gdk_draw_lines (drawable, gc, p, 9);
}


static char *
escape_filename (const char *text) 
{
	char *utf8_text;
	char *escaped_text;

	utf8_text = g_filename_to_utf8 (text, -1, NULL, NULL, NULL);
	g_return_val_if_fail (utf8_text != NULL, NULL);
	
	escaped_text = g_markup_escape_text (utf8_text, -1);
	g_free (utf8_text);

	return escaped_text;
}


static GdkPixmap*
get_pixmap (GdkDrawable    *drawable,
	    GdkGC          *gc,
	    PangoRectangle *rect)
{
	GdkPixmap *pixmap;

	pixmap = gdk_pixmap_new (drawable, rect->width, rect->height, -1);
	gdk_draw_drawable (pixmap,
			   gc,
			   drawable,
			   rect->x, rect->y,
			   0, 0,
			   rect->width, rect->height);

	return pixmap;
}


static int
pos_from_path (GList      *list,
	       const char *path)
{
	GList *scan;
	int    i = 0;

	if ((list == NULL) || (path == NULL))
		return 0;

	for (scan = list; scan; scan = scan->next) {
		char *l_path = scan->data;
		if (strcmp (l_path, path) == 0)
			return i;
		i++;
	}

	return 0;
}


static char *
get_file_info (GthFullscreen *fullscreen)
{
	GthFullscreenPrivateData *priv = fullscreen->priv;
	const char  *image_filename;
	ImageViewer *image_viewer = (ImageViewer*) fullscreen->priv->viewer;
	char        *e_filename;
	int          width, height;
	char        *size_txt;
	char        *file_size_txt;
	int          zoom;
	char        *file_info;
	char         time_txt[50], *utf8_time_txt;
	time_t       timer = 0;
	struct tm   *tm;

	image_filename = gth_window_get_image_filename (GTH_WINDOW (fullscreen));
	e_filename = escape_filename (file_name_from_path (image_filename));

	width = image_viewer_get_image_width (image_viewer);
	height = image_viewer_get_image_height (image_viewer);

	size_txt = g_strdup_printf (_("%d x %d pixels"), width, height);
	file_size_txt = gnome_vfs_format_file_size_for_display (get_file_size (image_filename));

	zoom = (int) (image_viewer->zoom_level * 100.0);

#ifdef HAVE_LIBEXIF
	timer = get_exif_time (image_filename);
#endif
	if (timer == 0)
		timer = get_file_mtime (image_filename);
	tm = localtime (&timer);
	strftime (time_txt, 50, _("%d %B %Y, %H:%M"), tm);
	utf8_time_txt = g_locale_to_utf8 (time_txt, -1, 0, 0, 0);

	file_info = g_strdup_printf ("<small><i>%s - %s (%d%%) - %s</i>\n%d/%d - <tt>%s</tt></small>",
				     utf8_time_txt,
				     size_txt,
				     zoom,
				     file_size_txt,
				     pos_from_path (priv->file_list, image_filename) + 1,
				     priv->files,
				     e_filename);

	g_free (utf8_time_txt);
	g_free (e_filename);
	g_free (size_txt);
	g_free (file_size_txt);

	return file_info;
}


static void
set_button_active_no_notify (gpointer      data,
			     GtkWidget    *button,
			     gboolean      active)
{
	g_signal_handlers_block_by_data (button, data);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), active);
	g_signal_handlers_unblock_by_data (button, data);
}


static void
show_comment_on_image (GthFullscreen *fullscreen)
{
	GthWindow      *window = GTH_WINDOW (fullscreen);
	GthFullscreenPrivateData *priv = fullscreen->priv;
	ImageViewer    *viewer = (ImageViewer*) priv->viewer;
	CommentData    *cdata = NULL;
	char           *comment, *e_comment, *file_info;
	char           *marked_text, *parsed_text;
	PangoLayout    *layout;
	PangoAttrList  *attr_list = NULL;
        GError         *error = NULL;
	int             text_x, text_y;
	GdkPixbuf      *icon;
	int             icon_x, icon_y, icon_w, icon_h;
	int             max_text_width;

	if (priv->comment_visible) 
		return;

	if (priv->buffer != NULL) {
		g_object_unref (priv->buffer);
		g_object_unref (priv->original_buffer);
		priv->buffer = NULL;
		priv->original_buffer = NULL;
	}

	if (gth_window_get_image_filename (window) != NULL)
		cdata = comments_load_comment (gth_window_get_image_filename (window));
	else
		return;

	priv->comment_visible = TRUE;
	set_button_active_no_notify (NULL, priv->info_button, TRUE);
	
	comment = NULL;
	if (cdata != NULL) {
		comment = comments_get_comment_as_string (cdata, "\n", " - ");
		comment_data_free (cdata);
	}

	file_info = get_file_info (fullscreen);

	if (comment == NULL)
		marked_text = g_strdup (file_info); 
	else {
		e_comment = g_markup_escape_text (comment, -1);
		marked_text = g_strdup_printf ("<b>%s</b>\n\n%s", 
					       e_comment, 
					       file_info);
		g_free (e_comment);
	}

	g_free (file_info);

	/**/

	layout = gtk_widget_create_pango_layout (GTK_WIDGET (viewer), NULL);
	pango_layout_set_wrap (layout, PANGO_WRAP_WORD);
	pango_layout_set_font_description (layout, GTK_WIDGET (viewer)->style->font_desc);
	pango_layout_set_alignment (layout, PANGO_ALIGN_LEFT);

	if (! pango_parse_markup (marked_text, -1,
				  0,
				  &attr_list, 
				  &parsed_text, 
				  NULL,
				  &error)) {
		g_warning ("Failed to set text from markup due to error parsing markup: %s\nThis is the text that caused the error: %s",  error->message, marked_text);
		g_error_free (error);
		g_free (marked_text);
		g_object_unref (layout);
		return;
	}
	g_free (marked_text);

	pango_layout_set_attributes (layout, attr_list);
        pango_attr_list_unref (attr_list);

	pango_layout_set_text (layout, parsed_text, strlen (parsed_text));
	g_free (parsed_text);

	icon = gdk_pixbuf_new_from_inline (-1, add_comment_24_rgba, FALSE, NULL);
	icon_w = gdk_pixbuf_get_width (icon);
	icon_h = gdk_pixbuf_get_height (icon);

	max_text_width = ((gdk_screen_width () * 3 / 4) 
			  - icon_w 
			  - (X_PADDING * 3) 
			  - (X_PADDING * 2));

	pango_layout_set_width (layout, max_text_width * PANGO_SCALE);
	pango_layout_get_pixel_extents (layout, NULL, &priv->bounds);

	priv->bounds.width += (2 * X_PADDING) + (icon_w + X_PADDING);
	priv->bounds.height += 2 * Y_PADDING;
	priv->bounds.height = MIN (gdk_screen_height () - icon_h - (Y_PADDING * 2), 
				   priv->bounds.height);

	priv->bounds.x = (gdk_screen_width () - priv->bounds.width) / 2;
	priv->bounds.y = gdk_screen_height () - priv->bounds.height - (Y_PADDING * 3);
	priv->bounds.x = MAX (priv->bounds.x, 0);
	priv->bounds.y = MAX (priv->bounds.y, 0);

	text_x = X_PADDING + icon_w + X_PADDING;
	text_y = Y_PADDING;
	icon_x = X_PADDING;
	icon_y = (priv->bounds.height - icon_h) / 2;

	priv->buffer = get_pixmap (GTK_WIDGET (viewer)->window,
				   GTK_WIDGET (viewer)->style->black_gc,
				   &priv->bounds);
	priv->original_buffer = get_pixmap (GTK_WIDGET (viewer)->window,
					    GTK_WIDGET (viewer)->style->black_gc,
					    &priv->bounds);
	render_background (priv->buffer, &priv->bounds);
	render_frame      (priv->buffer, 
			   GTK_WIDGET (viewer)->style->black_gc,
			   &priv->bounds);
	gdk_draw_layout (priv->buffer,
			 GTK_WIDGET (viewer)->style->black_gc,
			 text_x, 
			 text_y,
			 layout);
	gdk_pixbuf_render_to_drawable (icon,
				       priv->buffer,
				       GTK_WIDGET (viewer)->style->black_gc,
				       0, 0,
				       icon_x, icon_y,
				       icon_w, icon_h,
				       GDK_RGB_DITHER_NONE,
				       0, 0);
	g_object_unref (icon);
	g_object_unref (layout);

	gdk_draw_drawable (GTK_WIDGET (viewer)->window,
			   GTK_WIDGET (viewer)->style->black_gc,
			   priv->buffer,
			   0, 0,
			   priv->bounds.x, priv->bounds.y,
			   priv->bounds.width, priv->bounds.height);

	viewer->next_scroll_repaint = TRUE;
}


static void
hide_comment_on_image (GthFullscreen *fullscreen)
{
	GthFullscreenPrivateData *priv = fullscreen->priv;
	ImageViewer *viewer = (ImageViewer*) priv->viewer;

	viewer->next_scroll_repaint = FALSE;

	if (priv->original_buffer == NULL)
		return;

	priv->comment_visible = FALSE;
	set_button_active_no_notify (NULL, priv->info_button, FALSE);

	gdk_draw_drawable (priv->viewer->window,
			   priv->viewer->style->black_gc,
			   priv->original_buffer,
			   0, 0,
			   priv->bounds.x, priv->bounds.y,
			   priv->bounds.width, priv->bounds.height);
	gdk_flush ();
}


static void
_show_cursor__hide_comment (GthFullscreen *fullscreen)
{
	GthFullscreenPrivateData *priv = fullscreen->priv;

	if (priv->slideshow) {
		priv->slideshow_paused = TRUE;
		if (priv->pause_button != NULL)
			set_button_active_no_notify (NULL, priv->pause_button, TRUE);
	}

	if (priv->mouse_hide_id != 0)
		g_source_remove (priv->mouse_hide_id);
	priv->mouse_hide_id = 0;
	
	image_viewer_show_cursor ((ImageViewer*) priv->viewer);
	if (priv->comment_visible)
		hide_comment_on_image (fullscreen);
}


static void
delete_current_image (GthFullscreen *fullscreen)
{
	const char *image_filename;
	GList      *list;

	image_filename = gth_window_get_image_filename (GTH_WINDOW (fullscreen));
	if (image_filename == NULL)
		return;
	list = g_list_prepend (NULL, g_strdup (image_filename));

	if (fullscreen->priv->catalog_path == NULL)
		dlg_file_delete__confirm (GTH_WINDOW (fullscreen),
					  list,
					  _("The image will be moved to the Trash, are you sure?"));
	else
		remove_files_from_catalog (GTH_WINDOW (fullscreen),
					   fullscreen->priv->catalog_path,
					   list);
}


static gboolean
viewer_key_press_cb (GtkWidget   *widget, 
		     GdkEventKey *event,
		     gpointer     data)
{
	GthFullscreen *fullscreen = data;
	GthFullscreenPrivateData *priv = fullscreen->priv;
	GthWindow     *window = (GthWindow*) fullscreen;
	ImageViewer   *viewer = (ImageViewer*) fullscreen->priv->viewer;
	gboolean       retval = FALSE;

	switch (event->keyval) {

		/* Exit fullscreen mode. */
	case GDK_Escape:
	case GDK_q:
	case GDK_v:
	case GDK_f:
	case GDK_F11:
		gth_window_close (window);
		retval = TRUE;
		break;

	case GDK_s:
		if (fullscreen->priv->slideshow) {
			gth_window_close (window);
			retval = TRUE;
		}
		break;

		/* Zoom in. */
	case GDK_plus:
	case GDK_equal:
	case GDK_KP_Add:
		image_viewer_zoom_in (viewer);
		break;

		/* Zoom out. */
	case GDK_minus:
	case GDK_KP_Subtract:
		image_viewer_zoom_out (viewer);
		break;

		/* Actual size. */
	case GDK_KP_Divide:
	case GDK_1:
	case GDK_z:
		image_viewer_set_zoom (viewer, 1.0);
		break;

		/* Set zoom to 2.0. */
	case GDK_2:
		image_viewer_set_zoom (viewer, 2.0);
		break;

		/* Set zoom to 3.0. */
	case GDK_3:
		image_viewer_set_zoom (viewer, 3.0);
		break;

		/* Zoom to fit. */
	case GDK_x:
		image_viewer_zoom_to_fit (viewer);
		break;

		/* Toggle animation. */
	case GDK_g:
		gth_window_set_animation (window, ! gth_window_get_animation (window));
		break;

		/* Step animation. */
	case GDK_j:
        	gth_window_step_animation (window); 
		break;

		/* Load next image. */
	case GDK_space:
	case GDK_n:
	case GDK_Page_Down:
		load_next_image (fullscreen);
		break;

		/* Load previous image. */
	case GDK_p:
	case GDK_b:
	case GDK_BackSpace:
	case GDK_Page_Up:
		load_prev_image (fullscreen);
		break;

		/* Show first image. */
	case GDK_Home: 
	case GDK_KP_Home:
		priv->current = priv->file_list;
		load_current_image (fullscreen);
		break;
		
		/* Show last image. */
	case GDK_End: 
	case GDK_KP_End:
		priv->current = g_list_last (priv->file_list);
		load_current_image (fullscreen);
		break;

		/* Edit comment. */
	case GDK_c:
		_show_cursor__hide_comment (fullscreen);
		gth_window_edit_comment (GTH_WINDOW (fullscreen));
		break;

		/* Edit categories. */
	case GDK_k:
		_show_cursor__hide_comment (fullscreen);
		gth_window_edit_categories (window);
		break;

		/* View/Hide comment */
	case GDK_i:
		if (priv->comment_visible) {
			priv->image_data_visible = FALSE;
			hide_comment_on_image (fullscreen);
		} else {
			priv->image_data_visible = TRUE;
			show_comment_on_image (fullscreen);
		}
		break;

		/* Delete selection. */
	case GDK_Delete: 
	case GDK_KP_Delete:
		_show_cursor__hide_comment (fullscreen);
		delete_current_image (fullscreen);
		break;

	default:
		if (priv->comment_visible) 
			hide_comment_on_image (fullscreen);
		break;
	}

	return retval;
}


static gboolean 
fs_expose_event_cb (GtkWidget        *widget, 
		    GdkEventExpose   *event,
		    GthFullscreen    *fullscreen)
{
	if (fullscreen->priv->comment_visible) {
		fullscreen->priv->comment_visible = FALSE;
		show_comment_on_image (fullscreen);
	}

	return FALSE;
}


static gboolean 
fs_repainted_cb (GtkWidget     *widget, 
		 GthFullscreen *fullscreen)
{
	fullscreen->priv->comment_visible = FALSE;
	return TRUE;
}


static void
gth_fullscreen_construct (GthFullscreen *fullscreen,
			  GdkPixbuf     *image,
			  const char    *image_path,
			  GList         *file_list)
{
	GthFullscreenPrivateData *priv = fullscreen->priv;
	GdkScreen *screen = gtk_widget_get_screen (GTK_WIDGET (fullscreen));

	gtk_window_set_default_size (GTK_WINDOW (fullscreen),
				     gdk_screen_get_width (screen),
				     gdk_screen_get_height (screen));

	if (image != NULL) {
		priv->image = image;
		g_object_ref (image);
	}

	if (image_path != NULL)
		priv->image_path = g_strdup (image_path);
	else
		priv->image_path = NULL;

	priv->file_list = file_list;
	if (file_list != NULL)
		priv->files = MAX (g_list_length (priv->file_list), 1);
	else
		priv->files = 1;
	priv->current = NULL;

	priv->viewer = image_viewer_new ();
	image_viewer_set_zoom_quality (IMAGE_VIEWER (priv->viewer),
				       pref_get_zoom_quality ());
	image_viewer_set_zoom_change  (IMAGE_VIEWER (priv->viewer),
				       GTH_ZOOM_CHANGE_FIT_IF_LARGER);
	image_viewer_set_check_type   (IMAGE_VIEWER (priv->viewer),
				       pref_get_check_type ());
	image_viewer_set_check_size   (IMAGE_VIEWER (priv->viewer),
				       pref_get_check_size ());
	image_viewer_set_transp_type  (IMAGE_VIEWER (priv->viewer),
				       pref_get_transp_type ());
	image_viewer_set_black_background (IMAGE_VIEWER (priv->viewer), TRUE);
	image_viewer_hide_frame (IMAGE_VIEWER (priv->viewer));
	image_viewer_zoom_to_fit_if_larger (IMAGE_VIEWER (priv->viewer));

	g_signal_connect (G_OBJECT (priv->viewer),
			  "key_press_event",
			  G_CALLBACK (viewer_key_press_cb),
			  fullscreen);
	g_signal_connect_after (G_OBJECT (priv->viewer),
				"expose_event",
				G_CALLBACK (fs_expose_event_cb),
				fullscreen);
	g_signal_connect_after (G_OBJECT (priv->viewer),
				"repainted",
				G_CALLBACK (fs_repainted_cb),
				fullscreen);
	g_signal_connect_swapped (G_OBJECT (priv->viewer), 
				  "clicked",
				  G_CALLBACK (load_next_image), 
				  fullscreen);

	gtk_widget_show (priv->viewer);
	gnome_app_set_contents (GNOME_APP (fullscreen), priv->viewer);

	/**/

	g_signal_connect (G_OBJECT (fullscreen),
			  "motion_notify_event",
			  G_CALLBACK (motion_notify_event_cb),
			  fullscreen);
}


GtkWidget * 
gth_fullscreen_new (GdkPixbuf  *image,
		    const char *image_path,
		    GList      *file_list)
{
	GthFullscreen *fullscreen;

	fullscreen = (GthFullscreen*) g_object_new (GTH_TYPE_FULLSCREEN, NULL);
	gth_fullscreen_construct (fullscreen, image, image_path, file_list);

	return (GtkWidget*) fullscreen;
}


static GtkWidget*
create_button (const char *stock_icon,
	       const char *label,
	       gboolean    toggle)
{
	GtkWidget *button;
	GtkWidget *image;
	GtkWidget *button_box;

	if (toggle)
		button = gtk_toggle_button_new ();
	else
		button = gtk_button_new ();

	if (pref_get_real_toolbar_style() == GTH_TOOLBAR_STYLE_TEXT_BELOW)
		button_box = gtk_vbox_new (FALSE, 5);
	else
		button_box = gtk_hbox_new (FALSE, 5);
	gtk_container_add (GTK_CONTAINER (button), button_box);

	image = gtk_image_new_from_stock (stock_icon, GTK_ICON_SIZE_LARGE_TOOLBAR);
	gtk_box_pack_start (GTK_BOX (button_box), image, FALSE, FALSE, 0);

	if (label != NULL)
		gtk_box_pack_start (GTK_BOX (button_box), gtk_label_new (label), FALSE, FALSE, 0);
	else {
		gtk_button_set_use_stock (GTK_BUTTON (button), TRUE);
		gtk_button_set_label (GTK_BUTTON (button), stock_icon);
	}

	return button;
}


static void
pause_button_toggled_cb (GtkToggleButton *button,
			 GthFullscreen   *fullscreen)
{
	fullscreen->priv->slideshow_paused = gtk_toggle_button_get_active (button);
	if (!fullscreen->priv->slideshow_paused)
		load_next_image (fullscreen);
}


static void
image_comment_toggled_cb (GtkToggleButton *button,
			  GthFullscreen   *fullscreen)
{
	if (gtk_toggle_button_get_active (button)) {
		fullscreen->priv->image_data_visible = TRUE;
		show_comment_on_image (fullscreen);
	} else {
		fullscreen->priv->image_data_visible = FALSE;
		hide_comment_on_image (fullscreen);
	}
}


static void
create_toolbar_window (GthFullscreen *fullscreen)
{
	GthFullscreenPrivateData *priv = fullscreen->priv;
	GtkWidget *button;
	GtkWidget *hbox;

	priv->toolbar_window = gtk_window_new (GTK_WINDOW_POPUP);
	gtk_window_set_default_size (GTK_WINDOW (priv->toolbar_window),
				     gdk_screen_get_width (gtk_widget_get_screen (priv->toolbar_window)),
				     -1);
	gtk_container_set_border_width (GTK_CONTAINER (priv->toolbar_window), 0);

	hbox = gtk_hbox_new (FALSE, 6);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 0);
	gtk_container_add (GTK_CONTAINER (priv->toolbar_window), hbox);

	/* restore normal view */

	button = create_button (GTHUMB_STOCK_FULLSCREEN, _("Restore Normal View"), FALSE);
	gtk_box_pack_end (GTK_BOX (hbox), button, FALSE, FALSE, 0);
	g_signal_connect_swapped (G_OBJECT (button),
				  "clicked",
				  G_CALLBACK (gtk_widget_destroy),
				  fullscreen);

	/* previous image */

	priv->prev_button = button = create_button (GTHUMB_STOCK_PREVIOUS_IMAGE, _("Previous"), FALSE);
	gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);
	g_signal_connect_swapped (G_OBJECT (button),
				  "clicked",
				  G_CALLBACK (load_prev_image),
				  fullscreen);

	/* next image */

	priv->next_button = button = create_button (GTHUMB_STOCK_NEXT_IMAGE, _("Next"), FALSE);
	gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);
	g_signal_connect_swapped (G_OBJECT (button),
				  "clicked",
				  G_CALLBACK (load_next_image),
				  fullscreen);

	/* image comment */

	priv->info_button = button = create_button (GTHUMB_STOCK_PROPERTIES, _("Image Info"), TRUE);
	gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);
	g_signal_connect (G_OBJECT (button),
			  "toggled",
			  G_CALLBACK (image_comment_toggled_cb),
			  fullscreen);

	if (priv->slideshow) {
		/* pause slideshow */

		priv->pause_button = button = create_button (GTK_STOCK_MEDIA_PAUSE, _("Pause"), TRUE);
		gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);
		g_signal_connect (G_OBJECT (button),
				  "toggled",
				  G_CALLBACK (pause_button_toggled_cb),
				  fullscreen);
	}

	/**/

	gtk_widget_show_all (hbox);
}


static void 
gth_fullscreen_show (GtkWidget *widget)
{	
	GthFullscreen            *fullscreen = GTH_FULLSCREEN (widget);
	GthFullscreenPrivateData *priv = fullscreen->priv;

	GTK_WIDGET_CLASS (parent_class)->show (widget);

	if (!priv->first_time_show) 
		return;
	priv->first_time_show = FALSE;

	create_toolbar_window (fullscreen);

	priv->slideshow_direction = pref_get_slideshow_direction ();
	priv->slideshow_delay = eel_gconf_get_integer (PREF_SLIDESHOW_DELAY, DEF_SLIDESHOW_DELAY);
	priv->slideshow_wrap_around = eel_gconf_get_boolean (PREF_SLIDESHOW_WRAP_AROUND, FALSE);

	image_viewer_hide_cursor (IMAGE_VIEWER (priv->viewer));
	gtk_window_fullscreen (GTK_WINDOW (widget));
	load_first_image (fullscreen);
}


static void
gth_fullscreen_close (GthWindow *window)
{
	gtk_widget_destroy (GTK_WIDGET (window));
}


static const char *
gth_fullscreen_get_image_filename (GthWindow *window)
{
	GthFullscreen *fullscreen = (GthFullscreen*) window;
	return get_image_filename (fullscreen->priv->current);
}


static GList *
gth_fullscreen_get_file_list_selection (GthWindow *window)
{
	GthFullscreen            *fullscreen = GTH_FULLSCREEN (window);
	GthFullscreenPrivateData *priv = fullscreen->priv;

	if (priv->current == NULL)
		return NULL;
	return g_list_prepend (NULL, g_strdup (priv->current->data));
}


static void
gth_fullscreen_set_animation (GthWindow *window,
			       gboolean   play)
{
	GthFullscreen *fullscreen = GTH_FULLSCREEN (window);
	ImageViewer   *image_viewer = IMAGE_VIEWER (fullscreen->priv->viewer);

	if (play)
		image_viewer_start_animation (image_viewer);
	else
		image_viewer_stop_animation (image_viewer);
}


static gboolean       
gth_fullscreen_get_animation (GthWindow *window)
{
	GthFullscreen *fullscreen = GTH_FULLSCREEN (window);
	return IMAGE_VIEWER (fullscreen->priv->viewer)->play_animation;
}


static void           
gth_fullscreen_step_animation (GthWindow *window)
{
	GthFullscreen *fullscreen = GTH_FULLSCREEN (window);
	image_viewer_step_animation (IMAGE_VIEWER (fullscreen->priv->viewer));
}


static void
gth_fullscreen_class_init (GthFullscreenClass *class)
{
	GObjectClass   *gobject_class;
	GtkWidgetClass *widget_class;
	GthWindowClass *window_class;

	parent_class = g_type_class_peek_parent (class);
	gobject_class = (GObjectClass*) class;
	widget_class = (GtkWidgetClass*) class;
	window_class = (GthWindowClass*) class;

	gobject_class->finalize = gth_fullscreen_finalize;

	widget_class->show = gth_fullscreen_show;

	window_class->close = gth_fullscreen_close;
	/*
	window_class->get_image_viewer = gth_fullscreen_get_image_viewer;
	*/
	window_class->get_image_filename = gth_fullscreen_get_image_filename;
	/*
	window_class->get_image_modified = gth_fullscreen_get_image_modified;
	window_class->set_image_modified = gth_fullscreen_set_image_modified;
	window_class->save_pixbuf = gth_fullscreen_save_pixbuf;
	window_class->exec_pixbuf_op = gth_fullscreen_exec_pixbuf_op;

	window_class->set_categories_dlg = gth_fullscreen_set_categories_dlg;
	window_class->get_categories_dlg = gth_fullscreen_get_categories_dlg;
	window_class->set_comment_dlg = gth_fullscreen_set_comment_dlg;
	window_class->get_comment_dlg = gth_fullscreen_get_comment_dlg;
	window_class->reload_current_image = gth_fullscreen_reload_current_image;
	window_class->update_current_image_metadata = gth_fullscreen_update_current_image_metadata;
	*/
	window_class->get_file_list_selection = gth_fullscreen_get_file_list_selection;
	/*
	window_class->get_file_list_selection_as_fd = gth_fullscreen_get_file_list_selection_as_fd;

	*/
	window_class->set_animation = gth_fullscreen_set_animation;
	window_class->get_animation = gth_fullscreen_get_animation;
	window_class->step_animation = gth_fullscreen_step_animation;
	/*
	window_class->edit_comment = gth_fullscreen_edit_comment;
	window_class->edit_categories = gth_fullscreen_edit_categories;
	window_class->set_fullscreen = gth_fullscreen_set_fullscreen;
	window_class->get_fullscreen = gth_fullscreen_get_fullscreen;
	window_class->set_slideshow = gth_fullscreen_set_slideshow;
	window_class->get_slideshow = gth_fullscreen_get_slideshow;
	*/
}


GType
gth_fullscreen_get_type ()
{
        static GType type = 0;

        if (! type) {
                GTypeInfo type_info = {
			sizeof (GthFullscreenClass),
			NULL,
			NULL,
			(GClassInitFunc) gth_fullscreen_class_init,
			NULL,
			NULL,
			sizeof (GthFullscreen),
			0,
			(GInstanceInitFunc) gth_fullscreen_init
		};

		type = g_type_register_static (GTH_TYPE_WINDOW,
					       "GthFullscreen",
					       &type_info,
					       0);
	}

        return type;
}


void
gth_fullscreen_set_slideshow (GthFullscreen *fullscreen,
			      gboolean       slideshow)
{
	g_return_if_fail (GTH_IS_FULLSCREEN (fullscreen));
	fullscreen->priv->slideshow = slideshow;
}


void
gth_fullscreen_set_catalog (GthFullscreen *fullscreen,
			    const char    *catalog_path)
{
	g_return_if_fail (GTH_IS_FULLSCREEN (fullscreen));

	g_free (fullscreen->priv->catalog_path);
	fullscreen->priv->catalog_path = NULL;

	if (catalog_path != NULL)
		fullscreen->priv->catalog_path = g_strdup (catalog_path);
}