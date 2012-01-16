/* GSecret - GLib wrapper for Secret Service
 *
 * Copyright 2011 Collabora Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the licence or (at
 * your option) any later version.
 *
 * See the included COPYING file for more information.
 */

#include "config.h"

#include "gsecret-item.h"
#include "gsecret-private.h"
#include "gsecret-service.h"
#include "gsecret-types.h"
#include "gsecret-value.h"

#include <glib/gi18n-lib.h>

struct _GSecretItemPrivate {
	GSecretService *service;
};

G_DEFINE_TYPE (GSecretItem, gsecret_item, G_TYPE_DBUS_PROXY);

static void
gsecret_item_init (GSecretItem *self)
{
	self->pv = G_TYPE_INSTANCE_GET_PRIVATE (self, GSECRET_TYPE_ITEM, GSecretItemPrivate);
}

static void
gsecret_item_class_init (GSecretItemClass *klass)
{

}

void
gsecret_item_delete (GSecretItem *self,
                     GCancellable *cancellable,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
	const gchar *object_path;

	g_return_if_fail (GSECRET_IS_ITEM (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	object_path = g_dbus_proxy_get_object_path (G_DBUS_PROXY (self));
	gsecret_service_delete_path (self->pv->service, object_path,
	                             cancellable, callback, user_data);
}

gboolean
gsecret_item_delete_finish (GSecretItem *self,
                            GAsyncResult *result,
                            GError **error)
{
	g_return_val_if_fail (GSECRET_IS_ITEM (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	return gsecret_service_delete_path_finish (self->pv->service, result, error);
}

gboolean
gsecret_item_delete_sync (GSecretItem *self,
                          GCancellable *cancellable,
                          GError **error)
{
	const gchar *object_path;

	g_return_val_if_fail (GSECRET_IS_ITEM (self), FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	object_path = g_dbus_proxy_get_object_path (G_DBUS_PROXY (self));
	return gsecret_service_delete_path_sync (self->pv->service,
	                                         object_path, cancellable, error);
}

static void
on_item_get_secret_ready (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GSecretItem *self = GSECRET_ITEM (g_async_result_get_source_object (user_data));
	GError *error = NULL;
	GSecretValue *value;
	GVariant *ret;

	ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), result, &error);
	if (error == NULL) {
		value = _gsecret_service_decode_secret (self->pv->service, ret);
		if (value == NULL) {
			g_set_error (&error, GSECRET_ERROR, GSECRET_ERROR_PROTOCOL,
			             _("Received invalid secret from the secret storage"));
		}
		g_object_unref (ret);
	}

	if (error != NULL)
		g_simple_async_result_take_error (res, error);
	else
		g_simple_async_result_set_op_res_gpointer (res, value,
		                                           gsecret_value_unref);

	g_simple_async_result_complete (res);
	g_object_unref (res);
}

static void
on_service_ensure_session (GObject *source, GAsyncResult *result, gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GSecretItem *self = GSECRET_ITEM (g_async_result_get_source_object (user_data));
	GError *error = NULL;
	GCancellable *cancellable = NULL;
	const gchar *session_path;

	session_path = _gsecret_service_ensure_session_finish (self->pv->service,
	                                                      result, &cancellable, &error);
	if (error != NULL) {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);

	} else {
		g_assert (session_path != NULL && session_path[0] != '\0');
		g_dbus_proxy_call (G_DBUS_PROXY (self), "GetSecret",
		                   g_variant_new ("o", session_path),
		                   G_DBUS_CALL_FLAGS_NONE, -1, cancellable,
		                   on_item_get_secret_ready, g_object_ref (res));
	}

	g_clear_object (&cancellable);
	g_object_unref (res);
}

void
gsecret_item_get_secret (GSecretItem *self, GCancellable *cancellable,
                         GAsyncReadyCallback callback, gpointer user_data)
{
	GSimpleAsyncResult *res;

	g_return_if_fail (GSECRET_IS_ITEM (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (self), callback,
	                                 user_data, gsecret_item_get_secret);

	gsecret_service_ensure_session (self->pv->service, cancellable,
	                                on_service_ensure_session,
	                                g_object_ref (res));

	g_object_unref (res);
}

GSecretValue*
gsecret_item_get_secret_finish (GSecretItem *self, GAsyncResult *result,
                                GError **error)
{
	GSimpleAsyncResult *res;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (self),
	                      gsecret_item_get_secret), NULL);

	res = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (res, error))
		return NULL;

	return gsecret_value_ref (g_simple_async_result_get_op_res_gpointer (res));
}

GSecretValue*
gsecret_item_get_secret_sync (GSecretItem *self,
                              GCancellable *cancellable,
                              GError **error)
{
	const gchar *session_path;
	GSecretValue *value;
	GVariant *ret;

	session_path = gsecret_service_ensure_session_sync (self->pv->service,
	                                                    cancellable, error);
	if (session_path != NULL)
		return NULL;

	g_assert (session_path != NULL && session_path[0] != '\0');
	ret = g_dbus_proxy_call_sync (G_DBUS_PROXY (self), "GetSecret",
	                              g_variant_new ("o", session_path),
	                              G_DBUS_CALL_FLAGS_NONE, -1,
	                              cancellable, error);

	if (ret != NULL) {
		value = _gsecret_service_decode_secret (self->pv->service, ret);
		if (value == NULL) {
			g_set_error (error, GSECRET_ERROR, GSECRET_ERROR_PROTOCOL,
			             _("Received invalid secret from the secret storage"));
		}
	}

	g_object_unref (ret);
	return value;
}

GHashTable *
gsecret_item_get_attributes (GSecretItem *self)
{
	GHashTable *attributes;
	GVariant *variant;

	g_return_val_if_fail (GSECRET_IS_ITEM (self), NULL);

	variant = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), "Attributes");
	g_return_val_if_fail (variant != NULL, NULL);

	attributes = _gsecret_util_attributes_for_variant (variant);
	g_variant_unref (variant);

	return attributes;
}

void
gsecret_item_set_attributes (GSecretItem *self,
                             GHashTable *attributes,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
	GVariant *variant;

	g_return_if_fail (GSECRET_IS_ITEM (self));
	g_return_if_fail (attributes != NULL);

	variant = _gsecret_util_variant_for_attributes (attributes);

	_gsecret_util_set_property (G_DBUS_PROXY (self), "Attributes", variant,
	                            gsecret_item_set_attributes, cancellable,
	                            callback, user_data);

	g_variant_unref (variant);
}

gboolean
gsecret_item_set_attributes_finish (GSecretItem *self,
                                    GAsyncResult *result,
                                    GError **error)
{
	g_return_val_if_fail (GSECRET_IS_ITEM (self), FALSE);

	return _gsecret_util_set_property_finish (G_DBUS_PROXY (self),
	                                          gsecret_item_set_attributes,
	                                          result, error);
}

gboolean
gsecret_item_set_attributes_sync (GSecretItem *self,
                                  GHashTable *attributes,
                                  GCancellable *cancellable,
                                  GError **error)
{
	GVariant *variant;
	gboolean ret;

	g_return_val_if_fail (GSECRET_IS_ITEM (self), FALSE);
	g_return_val_if_fail (attributes != NULL, FALSE);

	variant = _gsecret_util_variant_for_attributes (attributes);

	ret = _gsecret_util_set_property_sync (G_DBUS_PROXY (self), "Attributes",
	                                       variant, cancellable, error);

	g_variant_unref (variant);

	return ret;
}

gchar *
gsecret_item_get_label (GSecretItem *self)
{
	GVariant *variant;
	gchar *label;

	g_return_val_if_fail (GSECRET_IS_ITEM (self), NULL);

	variant = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), "Label");
	g_return_val_if_fail (variant != NULL, NULL);

	label = g_variant_dup_string (variant, NULL);
	g_variant_unref (variant);

	return label;
}

void
gsecret_item_set_label (GSecretItem *self,
                        const gchar *label,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	g_return_if_fail (GSECRET_IS_ITEM (self));
	g_return_if_fail (label != NULL);

	_gsecret_util_set_property (G_DBUS_PROXY (self), "Label",
	                            g_variant_new_string (label),
	                            gsecret_item_set_label,
	                            cancellable, callback, user_data);
}

gboolean
gsecret_item_set_label_finish (GSecretItem *self,
                               GAsyncResult *result,
                               GError **error)
{
	g_return_val_if_fail (GSECRET_IS_ITEM (self), FALSE);

	return _gsecret_util_set_property_finish (G_DBUS_PROXY (self),
	                                          gsecret_item_set_label,
	                                          result, error);
}

gboolean
gsecret_item_set_label_sync (GSecretItem *self,
                             const gchar *label,
                             GCancellable *cancellable,
                             GError **error)
{
	g_return_val_if_fail (GSECRET_IS_ITEM (self), FALSE);
	g_return_val_if_fail (label != NULL, FALSE);

	return _gsecret_util_set_property_sync (G_DBUS_PROXY (self), "Label",
	                                       g_variant_new_string (label),
	                                       cancellable, error);
}

gboolean
gsecret_item_get_locked (GSecretItem *self)
{
	GVariant *variant;
	gboolean locked;

	g_return_val_if_fail (GSECRET_IS_ITEM (self), TRUE);

	variant = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), "Locked");
	g_return_val_if_fail (variant != NULL, TRUE);

	locked = g_variant_get_boolean (variant);
	g_variant_unref (variant);

	return locked;
}

guint64
gsecret_item_get_created (GSecretItem *self)
{
	GVariant *variant;
	guint64 created;

	g_return_val_if_fail (GSECRET_IS_ITEM (self), TRUE);

	variant = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), "Created");
	g_return_val_if_fail (variant != NULL, 0);

	created = g_variant_get_uint64 (variant);
	g_variant_unref (variant);

	return created;
}

guint64
gsecret_item_get_modified (GSecretItem *self)
{
	GVariant *variant;
	guint64 modified;

	g_return_val_if_fail (GSECRET_IS_ITEM (self), TRUE);

	variant = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), "Modified");
	g_return_val_if_fail (variant != NULL, 0);

	modified = g_variant_get_uint64 (variant);
	g_variant_unref (variant);

	return modified;
}