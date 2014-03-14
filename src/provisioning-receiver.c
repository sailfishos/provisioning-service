/*
 *  Copyright (C) 2014 Jolla Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <errno.h>
#include <stdio.h>
#include <dbus/dbus.h>
#include <glib.h>
#include <string.h>

#include "provisioning.h"
#include "log.h"
#include "provisioning-receiver.h"
#include "provisioning-decoder.h"


struct interface_data {
	char *name;
	const GDBusMethodTable *methods;
	const GDBusSignalTable *signal;
	void *user_data;
	GDBusDestroyFunction destroy;
};


struct watch_info {
	guint id;
	DBusWatch *watch;
	DBusConnection *conn;
};

struct generic_data {
	unsigned int refcount;
	DBusConnection *conn;
	char *path;
	GSList *interfaces;
	GSList *added;
//	GSList *removed;
	guint process_id;
};

static DBusConnection *g_connection;

DBusConnection *provisioning_dbus_get_connection(void)
{
	return g_connection;
}

static void unregister_func(DBusConnection *connection, void *user_data)
{
	struct generic_data *data = user_data;

	LOG("unregister_func");

	if (data->process_id > 0) {
		g_source_remove(data->process_id);
		data->process_id = 0;
	}

	dbus_connection_unref(data->conn);
	g_free(data->path);
	g_free(data);
}

static gboolean g_dbus_args_have_signature(const GDBusArgInfo *args,
							DBusMessage *message)
{
	const char *sig = dbus_message_get_signature(message);
	const char *p = NULL;

	for (; args && args->signature && *sig; args++) {
		p = args->signature;

		for (; *sig && *p; sig++, p++) {
			if (*p != *sig) {
				LOG("g_dbus_args_have_signature:%s,%s",
						p,sig);
				return FALSE;
			}
		}
	}

	if (*sig || (p && *p) || (args && args->signature))
		return FALSE;

	return TRUE;
}

static DBusHandlerResult process_message(DBusConnection *connection,
			DBusMessage *message, const GDBusMethodTable *method,
							void *iface_user_data)
{
	DBusMessage *reply;

	reply = method->function(connection, message, iface_user_data);

	if (reply != NULL) {
		dbus_message_unref(reply);
	}

	return DBUS_HANDLER_RESULT_HANDLED;
}

static struct interface_data *find_interface(GSList *interfaces,
						const char *name)
{
	GSList *list;

	if (name == NULL)
		return NULL;

	for (list = interfaces; list; list = list->next) {
		struct interface_data *iface = list->data;
		if (!strcmp(name, iface->name))
			return iface;
	}

	return NULL;
}

static DBusHandlerResult message_func(DBusConnection *connection,
					DBusMessage *message, void *user_data)
{
	struct generic_data *data = user_data;
	LOG("message_func");
	LOG("got dbus message sent to %s %s %s",
		dbus_message_get_destination(message),
		dbus_message_get_interface(message),
		dbus_message_get_path(message));

	struct interface_data *iface;
	const GDBusMethodTable *method;
	const char *interface;

	interface = dbus_message_get_interface(message);

	iface = find_interface(data->interfaces, interface);
	if (iface == NULL)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	for (method = iface->methods; method &&
			method->name && method->function; method++) {
		if (dbus_message_is_method_call(message, iface->name,
							method->name) == FALSE)
			continue;

		if (g_dbus_args_have_signature(method->in_args,
							message) == FALSE)
			continue;

		return process_message(connection, message, method,
							iface->user_data);
	}
	handle_exit(NULL);
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusObjectPathVTable generic_table = {
	.unregister_function	= unregister_func,
	.message_function	= message_func,
};

static gboolean add_interface(struct generic_data *data,
				const char *name,
				const GDBusMethodTable *methods,
				const GDBusSignalTable *signal,
				void *user_data,
				GDBusDestroyFunction destroy)
{
	struct interface_data *iface;

	iface = g_new0(struct interface_data, 1);
	iface->name = g_strdup(name);
	iface->methods = methods;
	iface->signal = signal;
	iface->user_data = user_data;
	iface->destroy = destroy;

	LOG("add_interface(iface->name:%s,iface->methods->name:%s)",
			iface->name,iface->methods->name);
	data->interfaces = g_slist_append(data->interfaces, iface);

	data->added = g_slist_append(data->added, iface);

	return TRUE;
}

static gboolean remove_interface(struct generic_data *data, const char *name)
{
	struct interface_data *iface;
	LOG("remove_interface");
	iface = find_interface(data->interfaces, name);
	if (iface == NULL)
		return FALSE;

	data->interfaces = g_slist_remove(data->interfaces, iface);

	if (iface->destroy) {
		iface->destroy(iface->user_data);
		iface->user_data = NULL;
	}

	if (g_slist_find(data->added, iface)) {
		data->added = g_slist_remove(data->added, iface);
		g_free(iface->name);
		g_free(iface);
		return TRUE;
	}

	g_free(iface);

	return TRUE;
}

static void object_path_unref(DBusConnection *connection, const char *path)
{
	struct generic_data *data = NULL;
	LOG("object_path_unref");
	if (dbus_connection_get_object_path_data(connection, path,
						(void *) &data) == FALSE)
		return;

	if (data == NULL)
		return;

	data->refcount--;

	if (data->refcount > 0)
		return;

	dbus_connection_unregister_object_path(data->conn, data->path);
}

static struct generic_data *object_path_ref(DBusConnection *connection,
							const char *path)
{
	struct generic_data *data;

	if (dbus_connection_get_object_path_data(connection, path,
						(void *) &data) == TRUE) {
		if (data != NULL) {
			data->refcount++;
			return data;
		}
	}

	data = g_new0(struct generic_data, 1);
	data->conn = dbus_connection_ref(connection);
	data->path = g_strdup(path);
	data->refcount = 1;
	LOG("object_path_ref(data->conn:%p,data->path:%s)",
			data->conn,data->path);
	if (!dbus_connection_register_object_path(connection, path,
						&generic_table, data)) {
		g_free(data);
		return NULL;
	}

	return data;
}

gboolean register_dbus_interface(DBusConnection *connection,
					const char *path, const char *name,
					const GDBusMethodTable *methods,
					const GDBusSignalTable *signal,
					void *user_data,
					GDBusDestroyFunction destroy)
{
	struct generic_data *data;

	data = object_path_ref(connection, path);
	if (data == NULL)
		return FALSE;
	LOG("register_dbus_interface: %p",data->conn);

	if (!add_interface(data, name, methods, signal, user_data, destroy)) {
		object_path_unref(connection, path);
		return FALSE;
	}

	return TRUE;
}

gboolean unregister_dbus_interface(DBusConnection *connection,
					const char *path, const char *name)
{
	struct generic_data *data = NULL;
	LOG("unregister_dbus_interface");
	if (path == NULL)
		return FALSE;

	if (dbus_connection_get_object_path_data(connection, path,
						(void *) &data) == FALSE)
		return FALSE;

	if (data == NULL)
		return FALSE;

	if (remove_interface(data, name) == FALSE)
		return FALSE;

	object_path_unref(connection, data->path);

	return TRUE;
}

void dbus_provisioning_set_connection(DBusConnection *conn)
{
	if (conn && g_connection != NULL)
		LOG("Setting a connection when it is not NULL");
	LOG("dbus_provisioning_set_connection:%p",conn);
	g_connection = conn;
}

gboolean g_dbus_request_name(DBusConnection *connection, const char *name,
							DBusError *error)
{
	int result;

	result = dbus_bus_request_name(connection, name,
					DBUS_NAME_FLAG_REPLACE_EXISTING/*DBUS_NAME_FLAG_DO_NOT_QUEUE*/, error);

	if (error != NULL) {
		if (dbus_error_is_set(error) == TRUE)
			return FALSE;
	}

	if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		if (error != NULL)
			dbus_set_error(error, name, "Name already in use");

		return FALSE;
	}

	return TRUE;
}

static gboolean message_dispatch(void *data)
{
	DBusConnection *conn = data;

	dbus_connection_ref(conn);

	/* Dispatch messages */
	while (dbus_connection_dispatch(conn) == DBUS_DISPATCH_DATA_REMAINS);

	dbus_connection_unref(conn);

	return FALSE;
}

static inline void queue_dispatch(DBusConnection *conn,
						DBusDispatchStatus status)
{
	if (status == DBUS_DISPATCH_DATA_REMAINS)
		g_timeout_add(0, message_dispatch, conn);
}

static gboolean watch_func(GIOChannel *chan, GIOCondition cond, gpointer data)
{
	struct watch_info *info = data;
	unsigned int flags = 0;
	DBusDispatchStatus status;
	DBusConnection *conn;

	conn = dbus_connection_ref(info->conn);

	if (cond & G_IO_IN)  flags |= DBUS_WATCH_READABLE;
	if (cond & G_IO_OUT) flags |= DBUS_WATCH_WRITABLE;
	if (cond & G_IO_HUP) flags |= DBUS_WATCH_HANGUP;
	if (cond & G_IO_ERR) flags |= DBUS_WATCH_ERROR;

	dbus_watch_handle(info->watch, flags);

	status = dbus_connection_get_dispatch_status(conn);
	queue_dispatch(conn, status);

	dbus_connection_unref(conn);

	return TRUE;
}

static void watch_info_free(void *data)
{
	struct watch_info *info = data;

	if (info->id > 0) {
		g_source_remove(info->id);
		info->id = 0;
	}

	dbus_connection_unref(info->conn);

	g_free(info);
}

static dbus_bool_t add_watch(DBusWatch *watch, void *data)
{
	DBusConnection *conn = data;
	GIOCondition cond = G_IO_HUP | G_IO_ERR;
	GIOChannel *chan;
	struct watch_info *info;
	unsigned int flags;
	int fd;

	if (!dbus_watch_get_enabled(watch))
		return TRUE;

	info = g_new0(struct watch_info, 1);

	fd = dbus_watch_get_unix_fd(watch);
	chan = g_io_channel_unix_new(fd);

	info->watch = watch;
	info->conn = dbus_connection_ref(conn);

	dbus_watch_set_data(watch, info, watch_info_free);

	flags = dbus_watch_get_flags(watch);

	if (flags & DBUS_WATCH_READABLE) cond |= G_IO_IN;
	if (flags & DBUS_WATCH_WRITABLE) cond |= G_IO_OUT;

	info->id = g_io_add_watch(chan, cond, watch_func, info);

	g_io_channel_unref(chan);

	return TRUE;
}

static void remove_watch(DBusWatch *watch, void *data)
{
	if (dbus_watch_get_enabled(watch))
		return;

	/* will trigger watch_info_free() */
	dbus_watch_set_data(watch, NULL, NULL);
}

static void watch_toggled(DBusWatch *watch, void *data)
{
	if (dbus_watch_get_enabled(watch))
		add_watch(watch, data);
	else
		remove_watch(watch, data);
}

static gboolean timeout_handler_dispatch(gpointer data)
{
	struct timeout_handler *handler = data;

	handler->id = 0;

	/* if not enabled should not be polled by the main loop */
	if (!dbus_timeout_get_enabled(handler->timeout))
		return FALSE;

	dbus_timeout_handle(handler->timeout);

	return FALSE;
}

static void timeout_handler_free(void *data)
{
	struct timeout_handler *handler = data;

	if (handler->id > 0) {
		g_source_remove(handler->id);
		handler->id = 0;
	}

	g_free(handler);
}

static dbus_bool_t add_timeout(DBusTimeout *timeout, void *data)
{
	int interval = dbus_timeout_get_interval(timeout);
	struct timeout_handler *handler;

	if (!dbus_timeout_get_enabled(timeout))
		return TRUE;

	handler = g_new0(struct timeout_handler, 1);

	handler->timeout = timeout;

	dbus_timeout_set_data(timeout, handler, timeout_handler_free);

	handler->id = g_timeout_add(interval, timeout_handler_dispatch,
								handler);

	return TRUE;
}

static void remove_timeout(DBusTimeout *timeout, void *data)
{
	/* will trigger timeout_handler_free() */
	dbus_timeout_set_data(timeout, NULL, NULL);
}

static void timeout_toggled(DBusTimeout *timeout, void *data)
{
	if (dbus_timeout_get_enabled(timeout))
		add_timeout(timeout, data);
	else
		remove_timeout(timeout, data);
}

static void dispatch_status(DBusConnection *conn,
					DBusDispatchStatus status, void *data)
{
	if (!dbus_connection_get_is_connected(conn))
		return;

	queue_dispatch(conn, status);
}

static void setup_dbus_with_main_loop(DBusConnection *conn)
{
	dbus_connection_set_watch_functions(conn, add_watch, remove_watch,
						watch_toggled, conn, NULL);

	dbus_connection_set_timeout_functions(conn, add_timeout, remove_timeout,
						timeout_toggled, NULL, NULL);

	dbus_connection_set_dispatch_status_function(conn, dispatch_status,
								NULL, NULL);
}

static gboolean setup_bus(DBusConnection *conn, const char *name,
						DBusError *error)
{
	gboolean result;
	DBusDispatchStatus status;

	if (name != NULL) {
		result = g_dbus_request_name(conn, name, error);

		if (error != NULL) {
			if (dbus_error_is_set(error) == TRUE)
				return FALSE;
		}

		if (result == FALSE)
			return FALSE;
	}

	setup_dbus_with_main_loop(conn);

	status = dbus_connection_get_dispatch_status(conn);
	queue_dispatch(conn, status);

	return TRUE;
}

DBusConnection *setup_dbus_bus(DBusBusType type, const char *name,
							DBusError *error)
{
	DBusConnection *conn;
	LOG("setup_dbus_bus");
	conn = dbus_bus_get(type, error);
	if (error != NULL) {
		if (dbus_error_is_set(error) == TRUE)
			return NULL;
	}
	if (conn == NULL)
		return NULL;
	if (setup_bus(conn, name, error) == FALSE) {
		dbus_connection_unref(conn);
		return NULL;
	}
	return conn;
}
