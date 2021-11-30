/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 *  Copyright (c) 2021 Matthew Leeds <mwleeds@protonmail.com>
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

#include "ephy-webapp-provider.h"

#include "ephy-web-app-utils.h"
#include "ephy-flatpak-utils.h"

#include <gio/gio.h>
#include <glib/gi18n.h>

struct _EphyWebAppProviderService {
  GApplication parent_instance;

  EphyWebAppProvider *skeleton;
};

struct _EphyWebAppProviderServiceClass {
  GApplicationClass parent_class;
};

G_DEFINE_TYPE (EphyWebAppProviderService, ephy_web_app_provider_service, G_TYPE_APPLICATION)

#define INACTIVITY_TIMEOUT 60 * 1000 /* One minute, in milliseconds */

static gboolean
handle_get_installed_web_apps (EphyWebAppProvider        *skeleton,
                               GDBusMethodInvocation     *invocation,
                               EphyWebAppProviderService *self)
{
  GVariantBuilder builder;
  GList *apps;
  GList *l;

  g_application_hold (G_APPLICATION (self));

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));

  apps = ephy_web_application_get_application_list ();
  for (l = apps; l; l = l->next) {
    EphyWebApplication *app = (EphyWebApplication *)l->data;
    g_autofree char *desktop_path = NULL;

    desktop_path = ephy_web_application_get_desktop_path (app);
    g_assert (desktop_path);

    g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{sv}"));
    g_variant_builder_add (&builder, "{sv}", "desktop-path",
                           g_variant_new_take_string (g_steal_pointer (&desktop_path)));
    g_variant_builder_add (&builder, "{sv}", "name",
                           g_variant_new_string (app->name));
    g_variant_builder_add (&builder, "{sv}", "url",
                           g_variant_new_string (app->url));
    g_variant_builder_add (&builder, "{sv}", "icon-path",
                           g_variant_new_string (app->icon_url));
    g_variant_builder_add (&builder, "{sv}", "install-date",
                           g_variant_new_uint64 (app->install_date_uint64));
    g_variant_builder_close (&builder);
  }

  ephy_web_app_provider_complete_get_installed_web_apps (skeleton, invocation,
                                                         g_variant_builder_end (&builder));

  ephy_web_application_free_application_list (apps);

  g_application_release (G_APPLICATION (self));

  return TRUE;
}

static gboolean
handle_install_web_app (EphyWebAppProvider        *skeleton,
                        GDBusMethodInvocation     *invocation,
                        char                      *url,
                        char                      *name,
                        char                      *icon_path,
                        char                      *install_token,
                        EphyWebAppProviderService *self)
{
  g_autofree char *id = NULL;
  g_autofree char *desktop_path = NULL;

  g_application_hold (G_APPLICATION (self));

  /* We need an install token acquired by a trusted system component such as
   * gnome-software because otherwise the Flatpak/Snap sandbox prevents us from
   * installing the app without using a portal (which would not be appropriate
   * since Epiphany is not the focused application).
   */
  if (ephy_is_running_inside_sandbox () &&
      (!install_token || *install_token == '\0')) {
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                                           "The install_token is required for sandboxed Epiphany");
    goto out;
  }
  if (!g_uri_is_valid (url, G_URI_FLAGS_NONE, NULL)) {
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                                           "The url passed was not valid");
    goto out;
  }
  if (!name || *name == '\0') {
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                                           "The name passed was not valid");
    goto out;
  }
  if (!icon_path || !g_path_is_absolute (icon_path)) {
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                                           "The icon_path passed was not valid");
    goto out;
  }

  id = ephy_web_application_get_app_id_from_name (name);

  /* Note: the icon_path is unlikely to be accessible to us from inside the
   * Flatpak sandbox but that's okay because we only need to pass it to a
   * portal.
   */
  desktop_path = ephy_web_application_create (id, url, name,
                                              NULL, /* icon_pixbuf */
                                              icon_path, install_token, EPHY_WEB_APPLICATION_NONE);
  if (!desktop_path) {
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                                           "Installing the web application '%s' (%s) failed", name, url);
    goto out;
  }

  ephy_web_app_provider_complete_install_web_app (skeleton, invocation);

out:
  g_application_release (G_APPLICATION (self));

  return TRUE;
}

static gboolean
handle_uninstall_web_app (EphyWebAppProvider        *skeleton,
                          GDBusMethodInvocation     *invocation,
                          char                      *desktop_path,
                          EphyWebAppProviderService *self)
{
  g_autoptr (EphyWebApplication) app = NULL;

  g_application_hold (G_APPLICATION (self));

  if (!desktop_path || !g_path_is_absolute (desktop_path)) {
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                                           "The desktop_path passed was not valid");
    goto out;
  }

  app = ephy_web_application_for_desktop_path (desktop_path);
  if (!app) {
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                                           "The desktop path '%s' does not correspond to an installed web app",
                                           desktop_path);
    goto out;
  }

  if (!ephy_web_application_delete (app->id)) {
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                                           "The web application '%s' could not be deleted", app->id);
    goto out;
  }

  ephy_web_app_provider_complete_uninstall_web_app (skeleton, invocation);

out:
  g_application_release (G_APPLICATION (self));

  return TRUE;
}

static void
ephy_web_app_provider_service_init (EphyWebAppProviderService *self)
{
  g_application_set_flags (G_APPLICATION (self), G_APPLICATION_IS_SERVICE);

  g_application_set_inactivity_timeout (G_APPLICATION (self), INACTIVITY_TIMEOUT);
}

static gboolean
ephy_web_app_provider_service_dbus_register (GApplication     *application,
                                             GDBusConnection  *connection,
                                             const gchar      *object_path,
                                             GError          **error)
{
  EphyWebAppProviderService *self;

  if (!G_APPLICATION_CLASS (ephy_web_app_provider_service_parent_class)->dbus_register (application,
                                                                                        connection,
                                                                                        object_path,
                                                                                        error))
    return FALSE;

  self = EPHY_WEB_APP_PROVIDER_SERVICE (application);
  self->skeleton = ephy_web_app_provider_skeleton_new ();

  ephy_web_app_provider_set_version (EPHY_WEB_APP_PROVIDER (self->skeleton), 1);
  ephy_web_app_provider_set_sandboxed (EPHY_WEB_APP_PROVIDER (self->skeleton),
                                       ephy_is_running_inside_sandbox ());

  g_signal_connect (self->skeleton, "handle-get-installed-web-apps",
                    G_CALLBACK (handle_get_installed_web_apps), self);
  g_signal_connect (self->skeleton, "handle-install-web-app",
                    G_CALLBACK (handle_install_web_app), self);
  g_signal_connect (self->skeleton, "handle-uninstall-web-app",
                    G_CALLBACK (handle_uninstall_web_app), self);

  return g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self->skeleton),
                                           connection, object_path, error);
}

static void
ephy_web_app_provider_service_dbus_unregister (GApplication    *application,
                                               GDBusConnection *connection,
                                               const gchar     *object_path)
{
  EphyWebAppProviderService *self;
  GDBusInterfaceSkeleton *skeleton;

  self = EPHY_WEB_APP_PROVIDER_SERVICE (application);
  skeleton = G_DBUS_INTERFACE_SKELETON (self->skeleton);
  if (g_dbus_interface_skeleton_has_connection (skeleton, connection))
    g_dbus_interface_skeleton_unexport_from_connection (skeleton, connection);

  g_clear_object (&self->skeleton);

  G_APPLICATION_CLASS (ephy_web_app_provider_service_parent_class)->dbus_unregister (application,
                                                                                     connection,
                                                                                     object_path);
}

static void
ephy_web_app_provider_service_dispose (GObject *object)
{
  G_OBJECT_CLASS (ephy_web_app_provider_service_parent_class)->dispose (object);
}

static void
ephy_web_app_provider_service_class_init (EphyWebAppProviderServiceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GApplicationClass *application_class = G_APPLICATION_CLASS (klass);

  object_class->dispose = ephy_web_app_provider_service_dispose;

  application_class->dbus_register = ephy_web_app_provider_service_dbus_register;
  application_class->dbus_unregister = ephy_web_app_provider_service_dbus_unregister;
}

EphyWebAppProviderService *
ephy_web_app_provider_service_new (void)
{
  g_autofree gchar *app_id = g_strconcat (APPLICATION_ID, ".WebAppProvider", NULL);

  return g_object_new (EPHY_TYPE_WEB_APP_PROVIDER_SERVICE,
                       "application-id", app_id,
                       NULL);
}
