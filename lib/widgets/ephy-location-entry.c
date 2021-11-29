/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 *  Copyright © 2002  Ricardo Fernández Pascual
 *  Copyright © 2003, 2004  Marco Pesenti Gritti
 *  Copyright © 2003, 2004, 2005  Christian Persch
 *  Copyright © 2008  Xan López
 *  Copyright © 2016  Igalia S.L.
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
#include "ephy-location-entry.h"

#include "ephy-widgets-type-builtins.h"
#include "ephy-about-handler.h"
#include "ephy-debug.h"
#include "ephy-embed-shell.h"
#include "ephy-gui.h"
#include "ephy-lib-type-builtins.h"
#include "ephy-settings.h"
#include "ephy-signal-accumulator.h"
#include "ephy-suggestion.h"
#include "ephy-title-widget.h"
#include "ephy-uri-helpers.h"

#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <string.h>
#include <webkit2/webkit2.h>

/**
 * SECTION:ephy-location-entry
 * @short_description: A location entry widget
 * @see_also: #GtkEntry
 *
 * #EphyLocationEntry implements the location bar in the main Epiphany window.
 */

struct _EphyLocationEntry {
  AdwBin parent_instance;

  GtkWidget *text;
  GtkWidget *progress;

  guint progress_timeout;
  gdouble progress_fraction;
};

static gboolean ephy_location_entry_reset_internal (EphyLocationEntry *,
                                                    gboolean);

enum {
  PROP_0,
  PROP_ADDRESS,
  PROP_SECURITY_LEVEL,
  LAST_PROP
};

enum signalsEnum {
  USER_CHANGED,
  BOOKMARK_CLICKED,
  GET_LOCATION,
  GET_TITLE,
  LAST_SIGNAL
};
static gint signals[LAST_SIGNAL] = { 0 };

static void ephy_location_entry_editable_init (GtkEditableInterface *iface);
static void ephy_location_entry_title_widget_interface_init (EphyTitleWidgetInterface *iface);
//static void schedule_dns_prefetch (EphyLocationEntry *self,
//                                   const gchar       *url);

G_DEFINE_TYPE_WITH_CODE (EphyLocationEntry, ephy_location_entry, ADW_TYPE_BIN,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_EDITABLE,
                                                ephy_location_entry_editable_init)
                         G_IMPLEMENT_INTERFACE (EPHY_TYPE_TITLE_WIDGET,
                                                ephy_location_entry_title_widget_interface_init))

static const char *
ephy_location_entry_title_widget_get_address (EphyTitleWidget *widget)
{
  return gtk_editable_get_text (GTK_EDITABLE (widget));
}

static void
ephy_location_entry_title_widget_set_address (EphyTitleWidget *widget,
                                              const char      *address)
{
  // TODO
  gtk_editable_set_text (GTK_EDITABLE (widget), address ? address : "");
}

static EphySecurityLevel
ephy_location_entry_title_widget_get_security_level (EphyTitleWidget *widget)
{
  return 0;
}

static void
ephy_location_entry_title_widget_set_security_level (EphyTitleWidget  *widget,
                                                     EphySecurityLevel security_level)
{
}

static void
ephy_location_entry_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  EphyLocationEntry *self = EPHY_LOCATION_ENTRY (object);

  if (gtk_editable_delegate_set_property (object, prop_id, value, pspec))
    {
      if (prop_id == LAST_PROP + GTK_EDITABLE_PROP_EDITABLE)
        {
          gtk_accessible_update_property (GTK_ACCESSIBLE (self),
                                          GTK_ACCESSIBLE_PROPERTY_READ_ONLY, !g_value_get_boolean (value),
                                          -1);
        }
      return;
    }

  switch (prop_id) {
    case PROP_ADDRESS:
      ephy_title_widget_set_address (EPHY_TITLE_WIDGET (self),
                                     g_value_get_string (value));
      break;
    case PROP_SECURITY_LEVEL:
      ephy_title_widget_set_security_level (EPHY_TITLE_WIDGET (self),
                                            g_value_get_enum (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
ephy_location_entry_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  EphyLocationEntry *self = EPHY_LOCATION_ENTRY (object);

  if (gtk_editable_delegate_get_property (object, prop_id, value, pspec))
    return;

  switch (prop_id) {
    case PROP_ADDRESS:
      g_value_set_string (value, ephy_title_widget_get_address (EPHY_TITLE_WIDGET (self)));
      break;
    case PROP_SECURITY_LEVEL:
      g_value_set_enum (value, ephy_title_widget_get_security_level (EPHY_TITLE_WIDGET (self)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
ephy_location_entry_title_widget_interface_init (EphyTitleWidgetInterface *iface)
{
  iface->get_address = ephy_location_entry_title_widget_get_address;
  iface->set_address = ephy_location_entry_title_widget_set_address;
  iface->get_security_level = ephy_location_entry_title_widget_get_security_level;
  iface->set_security_level = ephy_location_entry_title_widget_set_security_level;
}

static GtkEditable *
gtk_password_entry_get_delegate (GtkEditable *editable)
{
  EphyLocationEntry *self = EPHY_LOCATION_ENTRY (editable);

  return GTK_EDITABLE (self->text);
}

static void
ephy_location_entry_editable_init (GtkEditableInterface *iface)
{
  iface->get_delegate = gtk_password_entry_get_delegate;
}

static void
ephy_location_entry_dispose (GObject *object)
{
  EphyLocationEntry *self = EPHY_LOCATION_ENTRY (object);

  g_clear_handle_id (&self->progress_timeout, g_source_remove);

  if (self->text)
    gtk_editable_finish_delegate (GTK_EDITABLE (self));

  g_clear_pointer (&self->text, gtk_widget_unparent);
  g_clear_pointer (&self->progress, gtk_widget_unparent);

  G_OBJECT_CLASS (ephy_location_entry_parent_class)->dispose (object);
}

static void
ephy_location_entry_class_init (EphyLocationEntryClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = ephy_location_entry_get_property;
  object_class->set_property = ephy_location_entry_set_property;
  object_class->dispose = ephy_location_entry_dispose;

  g_object_class_override_property (object_class, PROP_ADDRESS, "address");
  g_object_class_override_property (object_class, PROP_SECURITY_LEVEL, "security-level");

  gtk_editable_install_properties (object_class, LAST_PROP);

  /**
   * EphyLocationEntry::user-changed:
   * @entry: the object on which the signal is emitted
   *
   * Emitted when the user changes the contents of the internal #GtkEntry
   *
   */
  signals[USER_CHANGED] = g_signal_new ("user_changed", G_OBJECT_CLASS_TYPE (klass),
                                        G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                        0, NULL, NULL, NULL,
                                        G_TYPE_NONE,
                                        0,
                                        G_TYPE_NONE);

  /**
   * EphyLocationEntry::bookmark-clicked:
   * @entry: the object on which the signal is emitted
   *
   * Emitted when the user clicks the bookmark icon inside the
   * #EphyLocationEntry.
   *
   */
  signals[BOOKMARK_CLICKED] = g_signal_new ("bookmark-clicked", G_OBJECT_CLASS_TYPE (klass),
                                            G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                            0, NULL, NULL, NULL,
                                            G_TYPE_NONE,
                                            0);

  /**
   * EphyLocationEntry::get-location:
   * @entry: the object on which the signal is emitted
   * Returns: the current page address as a string
   *
   * For drag and drop purposes, the location bar will request you the
   * real address of where it is pointing to. The signal handler for this
   * function should return the address of the currently loaded site.
   *
   */
  signals[GET_LOCATION] = g_signal_new ("get-location", G_OBJECT_CLASS_TYPE (klass),
                                        G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                        0, ephy_signal_accumulator_string,
                                        NULL, NULL,
                                        G_TYPE_STRING,
                                        0,
                                        G_TYPE_NONE);

  /**
   * EphyLocationEntry::get-title:
   * @entry: the object on which the signal is emitted
   * Returns: the current page title as a string
   *
   * For drag and drop purposes, the location bar will request you the
   * title of where it is pointing to. The signal handler for this
   * function should return the title of the currently loaded site.
   *
   */
  signals[GET_TITLE] = g_signal_new ("get-title", G_OBJECT_CLASS_TYPE (klass),
                                     G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                                     0, ephy_signal_accumulator_string,
                                     NULL, NULL,
                                     G_TYPE_STRING,
                                     0,
                                     G_TYPE_NONE);

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_css_name (widget_class, "entry");
}

static void
ephy_location_entry_init (EphyLocationEntry *self)
{
  self->text = gtk_text_new ();
  gtk_text_set_placeholder_text (GTK_TEXT (self->text),
                                 _("Search for websites, bookmarks, and open tabs"));
  gtk_text_set_input_hints (GTK_TEXT (self->text), GTK_INPUT_HINT_NO_EMOJI);
  gtk_widget_set_parent (self->text, GTK_WIDGET (self));

  self->progress =  g_object_new (GTK_TYPE_PROGRESS_BAR,
                                  "css-name", "progress",
                                  NULL);
  gtk_widget_set_can_target (self->progress, FALSE);
  gtk_widget_set_valign (self->progress, GTK_ALIGN_END);
  gtk_widget_set_parent (self->progress, GTK_WIDGET (self));

  gtk_editable_init_delegate (GTK_EDITABLE (self));

  gtk_editable_set_max_width_chars (GTK_EDITABLE (self), 200);
}

GtkWidget *
ephy_location_entry_new (void)
{
  return GTK_WIDGET (g_object_new (EPHY_TYPE_LOCATION_ENTRY, NULL));
}

#if 0
typedef struct {
  GUri *uri;
  EphyLocationEntry *entry;
} PrefetchHelper;

static void
free_prefetch_helper (PrefetchHelper *helper)
{
  g_uri_unref (helper->uri);
  g_object_unref (helper->entry);
  g_free (helper);
}

static gboolean
do_dns_prefetch (PrefetchHelper *helper)
{
  EphyEmbedShell *shell = ephy_embed_shell_get_default ();

  if (helper->uri)
    webkit_web_context_prefetch_dns (ephy_embed_shell_get_web_context (shell), g_uri_get_host (helper->uri));

  helper->entry->dns_prefetch_handle_id = 0;

  return G_SOURCE_REMOVE;
}

/*
 * Note: As we do not have access to WebKitNetworkProxyMode, and because
 * Epiphany does not ever change it, we are just checking system default proxy.
 */
static void
proxy_resolver_ready_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  PrefetchHelper *helper = user_data;
  GProxyResolver *resolver = G_PROXY_RESOLVER (object);
  g_autoptr (GError) error = NULL;
  g_auto (GStrv) proxies = NULL;

  proxies = g_proxy_resolver_lookup_finish (resolver, result, &error);
  if (error != NULL) {
    free_prefetch_helper (helper);
    return;
  }

  if (proxies != NULL && (g_strv_length (proxies) > 1 || g_strcmp0 (proxies[0], "direct://") != 0)) {
    free_prefetch_helper (helper);
    return;
  }

  g_clear_handle_id (&helper->entry->dns_prefetch_handle_id, g_source_remove);
  helper->entry->dns_prefetch_handle_id =
    g_timeout_add_full (G_PRIORITY_DEFAULT,
                        250,
                        (GSourceFunc)do_dns_prefetch,
                        helper,
                        (GDestroyNotify)free_prefetch_helper);
  g_source_set_name_by_id (helper->entry->dns_prefetch_handle_id, "[epiphany] do_dns_prefetch");
}

static void
schedule_dns_prefetch (EphyLocationEntry *self,
                       const gchar       *url)
{
  GProxyResolver *resolver = g_proxy_resolver_get_default ();
  PrefetchHelper *helper;
  g_autoptr (GUri) uri = NULL;

  if (resolver == NULL)
    return;

  uri = g_uri_parse (url, G_URI_FLAGS_NONE, NULL);
  if (!uri || !g_uri_get_host (uri))
    return;

  helper = g_new0 (PrefetchHelper, 1);
  helper->entry = g_object_ref (self);
  helper->uri = g_steal_pointer (&uri);

  g_proxy_resolver_lookup_async (resolver, url, NULL, proxy_resolver_ready_cb, helper);
}
#endif

/**
 * ephy_location_entry_get_can_undo:
 * @entry: an #EphyLocationEntry widget
 *
 * Wheter @entry can restore the displayed user modified text to the unmodified
 * previous text.
 *
 * Return value: TRUE or FALSE indicating if the text can be restored
 *
 **/
gboolean
ephy_location_entry_get_can_undo (EphyLocationEntry *self)
{
  return FALSE;
}

/**
 * ephy_location_entry_get_can_redo:
 * @entry: an #EphyLocationEntry widget
 *
 * Wheter @entry can restore the displayed text to the user modified version
 * before the undo.
 *
 * Return value: TRUE or FALSE indicating if the text can be restored
 *
 **/
gboolean
ephy_location_entry_get_can_redo (EphyLocationEntry *self)
{
  return FALSE;
}

static gboolean
ephy_location_entry_reset_internal (EphyLocationEntry *self,
                                    gboolean           notify)
{
}

/**
 * ephy_location_entry_undo_reset:
 * @entry: an #EphyLocationEntry widget
 *
 * Undo a previous ephy_location_entry_reset.
 *
 **/
void
ephy_location_entry_undo_reset (EphyLocationEntry *self)
{
}

/**
 * ephy_location_entry_reset:
 * @entry: an #EphyLocationEntry widget
 *
 * Restore the @entry to the text corresponding to the current location, this
 * does not fire the user_changed signal. This is called each time the user
 * presses Escape while the location entry is selected.
 *
 * Return value: TRUE on success, FALSE otherwise
 *
 **/
gboolean
ephy_location_entry_reset (EphyLocationEntry *self)
{
  return FALSE;
}

/**
 * ephy_location_entry_focus:
 * @self: an #EphyLocationEntry widget
 *
 * Set focus on @self and select the text whithin. This is called when the
 * user hits Control+L.
 *
 **/
void
ephy_location_entry_focus (EphyLocationEntry *self)
{
  gtk_widget_grab_focus (self->text);
}

void
ephy_location_entry_set_bookmark_icon_state (EphyLocationEntry     *self,
                                             EphyBookmarkIconState  state)
{
}

/**
 * ephy_location_entry_set_lock_tooltip:
 * @entry: an #EphyLocationEntry widget
 * @tooltip: the text to be set in the tooltip for the lock icon
 *
 * Set the text to be displayed when hovering the lock icon of @entry.
 *
 **/
void
ephy_location_entry_set_lock_tooltip (EphyLocationEntry *self,
                                      const char        *tooltip)
{
}

void
ephy_location_entry_set_add_bookmark_popover (EphyLocationEntry *self,
                                              GtkPopover        *popover)
{
}

GtkPopover *
ephy_location_entry_get_add_bookmark_popover (EphyLocationEntry *self)
{
  return NULL;
}

GtkWidget *
ephy_location_entry_get_bookmark_widget (EphyLocationEntry *self)
{
  return NULL;
}

GtkWidget *
ephy_location_entry_get_reader_mode_widget (EphyLocationEntry *self)
{
  return NULL;
}

void
ephy_location_entry_set_reader_mode_visible (EphyLocationEntry *self,
                                             gboolean           visible)
{
}

void
ephy_location_entry_set_reader_mode_state (EphyLocationEntry *self,
                                           gboolean           active)
{
}

gboolean
ephy_location_entry_get_reader_mode_state (EphyLocationEntry *self)
{
  return FALSE;
}

static gboolean
progress_hide (gpointer user_data)
{
  EphyLocationEntry *self = EPHY_LOCATION_ENTRY (user_data);

  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (self->progress), 0);

  g_clear_handle_id (&self->progress_timeout, g_source_remove);

  return G_SOURCE_REMOVE;
}

static gboolean
ephy_location_entry_set_fraction_internal (gpointer user_data)
{
  EphyLocationEntry *self = EPHY_LOCATION_ENTRY (user_data);
  gint ms;
  gdouble progress;
  gdouble current;

  self->progress_timeout = 0;
  current = gtk_progress_bar_get_fraction (GTK_PROGRESS_BAR (self->progress));

  if ((self->progress_fraction - current) > 0.5 || self->progress_fraction == 1.0)
    ms = 10;
  else
    ms = 25;

  progress = current + 0.025;
  if (progress < self->progress_fraction) {
    gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (self->progress), progress);
    self->progress_timeout = g_timeout_add (ms, ephy_location_entry_set_fraction_internal, self);
  } else {
    gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (self->progress), self->progress_fraction);
    if (self->progress_fraction == 1.0)
      self->progress_timeout = g_timeout_add (500, progress_hide, self);
  }

  return G_SOURCE_REMOVE;
}

void
ephy_location_entry_set_progress (EphyLocationEntry *self,
                                  gdouble            fraction,
                                  gboolean           loading)
{
  gdouble current_progress;

  g_clear_handle_id (&self->progress_timeout, g_source_remove);

  if (!loading) {
    /* Setting progress to 0 when it is already 0 can actually cause the
     * progress bar to be shown. Yikes....
     */
    current_progress = gtk_progress_bar_get_fraction (GTK_PROGRESS_BAR (self->progress));
    if (current_progress != 0.0)
      gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (self->progress), 0.0);
    return;
  }

  self->progress_fraction = fraction;
  ephy_location_entry_set_fraction_internal (self);
}

void
ephy_location_entry_set_adaptive_mode (EphyLocationEntry *self,
                                       EphyAdaptiveMode   adaptive_mode)
{
}

void
ephy_location_entry_page_action_add (EphyLocationEntry *self,
                                     GtkWidget         *action)
{
}

static void
clear_page_actions (GtkWidget *child,
                    gpointer   user_data)
{
}

void
ephy_location_entry_page_action_clear (EphyLocationEntry *self)
{
}

void
ephy_location_bar_grab_focus_without_selecting (EphyLocationEntry *entry)
{
}

