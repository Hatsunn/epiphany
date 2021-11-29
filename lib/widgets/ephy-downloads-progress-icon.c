/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 *  Copyright © 2015 Igalia S.L.
 *
 *  This file is part of Epiphany.
 *
 *  Epiphany is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Epiphany is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Epiphany.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include "ephy-downloads-progress-icon.h"

#include "ephy-downloads-manager.h"
#include "ephy-embed-shell.h"

struct _EphyDownloadsProgressIcon {
  GtkDrawingArea parent_instance;
};

G_DEFINE_TYPE (EphyDownloadsProgressIcon, ephy_downloads_progress_icon, GTK_TYPE_DRAWING_AREA)

static void
ephy_downloads_progress_icon_draw (GtkDrawingArea *area,
                                   cairo_t        *cr,
                                   int             width,
                                   int             height,
                                   gpointer        user_data)
{
  EphyDownloadsManager *manager;
  GtkStyleContext *style_context;
  GdkRGBA color;
  gdouble progress;

  manager = ephy_embed_shell_get_downloads_manager (ephy_embed_shell_get_default ());
  progress = ephy_downloads_manager_get_estimated_progress (manager);

  style_context = gtk_widget_get_style_context (GTK_WIDGET (area));
  gtk_style_context_get_color (style_context, &color);
  color.alpha *= progress == 1 ? 1 : 0.2;

  gdk_cairo_set_source_rgba (cr, &color);
  cairo_move_to (cr, width / 4., 0);
  cairo_line_to (cr, width - (width / 4.), 0);
  cairo_line_to (cr, width - (width / 4.), height / 2.);
  cairo_line_to (cr, width, height / 2.);
  cairo_line_to (cr, width / 2., height);
  cairo_line_to (cr, 0, height / 2.);
  cairo_line_to (cr, width / 4., height / 2.);
  cairo_line_to (cr, width / 4., 0);
  cairo_fill_preserve (cr);

  if (progress > 0 && progress < 1) {
    cairo_clip (cr);
    color.alpha = 1;
    gdk_cairo_set_source_rgba (cr, &color);
    cairo_rectangle (cr, 0, 0, width, height * progress);
    cairo_fill (cr);
  }
}

static void
ephy_downloads_progress_icon_class_init (EphyDownloadsProgressIconClass *klass)
{
}

static void
ephy_downloads_progress_icon_init (EphyDownloadsProgressIcon *icon)
{
  g_object_set (icon, "width-request", 16, "height-request", 16, NULL);

  gtk_drawing_area_set_content_width (GTK_DRAWING_AREA (icon), 16);
  gtk_drawing_area_set_content_height (GTK_DRAWING_AREA (icon), 16);
  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (icon),
                                  ephy_downloads_progress_icon_draw,
                                  NULL, NULL);
}

GtkWidget *
ephy_downloads_progress_icon_new (void)
{
  return GTK_WIDGET (g_object_new (EPHY_TYPE_DOWNLOADS_PROGRESS_ICON, NULL));
}
