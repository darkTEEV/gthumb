/* -*- Mode: CPP; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  GThumb
 *
 *  Copyright (C) 2003-2009 Free Software Foundation, Inc.
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

#ifndef EXIV2_UTILS_H
#define EXIV2_UTILS_H

#include <glib.h>
#include <gio/gio.h>
#include <gthumb.h>

G_BEGIN_DECLS

gboolean   exiv2_read_metadata_from_file    (GFile           *file,
					     GFileInfo       *info,
					     GError         **error);
gboolean   exiv2_read_metadata_from_buffer  (void            *buffer,
					     gsize            buffer_size,
					     GFileInfo       *info,
					     GError         **error);
gboolean   exiv2_read_sidecar               (GFile           *file,
					     GFileInfo       *info);
gboolean   exiv2_supports_writes            (const char      *mime_type);
gboolean   exiv2_write_metadata  	    (SavePixbufData  *data);
gboolean   exiv2_write_metadata_to_buffer   (void           **buffer,
					     gsize           *buffer_size,
					     GFileInfo       *info,
					     GdkPixbuf       *pixbuf, /* optional */
					     GError         **error);
GdkPixbuf *exiv2_generate_thumbnail         (const char      *uri,
					     const char      *mime_type,
					     int              size);

G_END_DECLS

#endif /* EXIV2_UTILS_H */
