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

#include <string.h>
#include <glib.h>
#include <dbus/dbus.h>
#include "provisioning.h"
#include "log.h"
#include "provisioning-receiver.h"
#include "provisioning-decoder.h"
#include "provisioning-ofono.h"


#define PROVISIONING_SERVICE "org.nemomobile.provisioning"
#define PROVISIONING_SERVICE_INTERFACE "org.nemomobile.provisioning.interface"
#define PROVISIONING_SERVICE_PATH "/"

static GMainLoop *loop;
GSList *msglist;

struct provisioning_request {
	char *array; /* Byte array containing client request*/
	int array_len; /* Length of request byte array */
};

static gboolean handle_message(char *array, int array_len);

static void provisioning_exit(void)
{
	g_main_loop_quit(loop);
}

gboolean handle_exit(gpointer user_data)
{
	struct timeout_handler *handler = user_data;
	struct provisioning_request *req;

	if (handler) {
		handler->id = 0;
		g_free(handler);
	}

	GSList *l = NULL;
	if (msglist != NULL)
		l = g_slist_last(msglist);

	if (l != NULL) {
		req = l->data;
		g_free(req->array);
		g_free(req);
		msglist = g_slist_remove(msglist,l->data);
	}

	if (msglist == NULL) {
		provisioning_exit();
		LOG("provisioning_exit");
	} else {
		l = g_slist_last(msglist);
		if (l != NULL) {
			req = l->data;
			handle_message(req->array,req->array_len);
		}
	}

	return FALSE;
}

void send_signal(guint message)
{
	DBusConnection *conn = provisioning_dbus_get_connection();
	DBusMessage *msg;
	const char *name;

	LOG("send_signal");

	switch (message) {
	case PROV_SUCCESS:
		name = "apnProvisioningSucceeded";
		break;
	case PROV_PARTIAL_SUCCESS:
		name = "apnProvisioningPartiallySucceeded";
		break;
	default:
		name = "apnProvisioningFailed";
		break;
	}

	msg = dbus_message_new_signal(PROVISIONING_SERVICE_PATH, // object name of the signal
					PROVISIONING_SERVICE_INTERFACE, // interface name of the signal
					name); // name of the signal

	if (msg == NULL)
		goto out;

	if (!dbus_connection_send(conn, msg, NULL))
		goto out;

	dbus_connection_flush(conn);
out:
	dbus_message_unref(msg);
}

static gboolean handle_message(char *array, int array_len)
{

	LOG("handle_message");

	if (array_len < 0)
		goto error;

#ifdef FILEWRITE
	char *file_name = "received_wbxml";
	print_to_file(array, array_len, file_name);
#endif

	if (!decode_provisioning_wbxml(array, array_len)) {
		send_signal(PROV_FAILURE);
		goto error;
	}

	if (provisioning_init_ofono() < 0) {
		provisioning_exit_ofono();
		goto error;
	}

	LOG("provisioning_handle_message: SUCCESS");
	return TRUE;
error:
	return FALSE;

}

static DBusMessage *provisioning_handle_message(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct provisioning_request *req, *lastreq;
	char *array; /* Byte array containing client request*/
	int array_len; /* Length of request byte array */
	DBusMessageIter iter;
	DBusMessageIter subiter;
	GSList *l;

	LOG("provisioning_handle_message");

	if (exit_handler) {
		if(exit_handler->id > 0) {
			g_source_remove(exit_handler->id);
			exit_handler->id = 0;
		}
		g_free(exit_handler);
	}

	dbus_message_iter_init(msg, &iter);

	/*Let's first skip the parts we are not interested*/
	dbus_message_iter_next(&iter);	// 's'
	dbus_message_iter_next(&iter);	// 's'
	dbus_message_iter_next(&iter);	// 'u'
	dbus_message_iter_next(&iter);	// 'u'
	dbus_message_iter_next(&iter);	// 'i'
	dbus_message_iter_next(&iter);	// 'i'
	dbus_message_iter_next(&iter);	// 's'

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
		goto error;

	if (dbus_message_iter_get_element_type(&iter) != DBUS_TYPE_BYTE) {
		LOG("Ignoring dbus request because request element type=%c",
			dbus_message_iter_get_element_type(&iter));
		goto error;
	}

	dbus_message_iter_recurse(&iter, &subiter);

	dbus_message_iter_get_fixed_array(&subiter, &array, &array_len);

	req = g_try_new0(struct provisioning_request, 1);
	if (req == NULL)
		goto error;

	req->array_len = array_len;
	req->array = g_try_new0(char, req->array_len);
	if (req->array == NULL)
		goto error;

	memcpy(req->array, array, req->array_len);

	msglist = g_slist_prepend(msglist, req);

	l = g_slist_last(msglist);
	lastreq = l->data;
	if (lastreq == req)
		if (handle_message(lastreq->array,lastreq->array_len))
			return NULL;

error:
	LOG("provisioning failed");
	exit_handler = g_new0(struct timeout_handler, 1);
	exit_handler->id = g_timeout_add_seconds(1, handle_exit, exit_handler);
	return NULL;
}



static const GDBusMethodTable provisioning_methods[] = {
	{ GDBUS_METHOD("HandleProvisioningMessage",
			GDBUS_ARGS({ "provisioning_message", "ssuuiisay" }),
			NULL, provisioning_handle_message) },
	{ }
};

static const GDBusSignalTable provisioning_signals[] = {
	{ GDBUS_SIGNAL("apnProvisioningSucceeded", NULL) },
	{ GDBUS_SIGNAL("apnProvisioningPartiallySucceeded", NULL) },
	{ GDBUS_SIGNAL("apnProvisioningFailed", NULL) },
	{ }
};

static gboolean provisioning_init(void)
{
	DBusConnection *conn = provisioning_dbus_get_connection();
	gboolean ret;

	LOG("provisioning_init:%p",conn);
	ret = register_dbus_interface(conn, PROVISIONING_SERVICE_PATH,
					PROVISIONING_SERVICE_INTERFACE,
					provisioning_methods,
					provisioning_signals,
					NULL, NULL);

	return ret;
}

static void provisioning_cleanup(void)
{
	DBusConnection *conn = provisioning_dbus_get_connection();

	unregister_dbus_interface(conn, PROVISIONING_SERVICE_PATH,
					PROVISIONING_SERVICE_INTERFACE);
}
static gint debug_target = 0;

static GOptionEntry entries[] = {
	{ "debug", 'd', 0,G_OPTION_ARG_INT, &debug_target,
				"Options: 1(system logger) 2(stdout) 3(stderr)",
				"1..3" },
	{ NULL },
};

int main( int argc, char **argv )
{
	GOptionContext *context;
	GError *error = NULL;
	DBusConnection* conn;
	DBusError err;

	context = g_option_context_new(NULL);

	g_option_context_add_main_entries(context, entries, NULL);
	if (g_option_context_parse(context, &argc, &argv, &error) == FALSE) {
		if (error != NULL) {
			g_printerr("%s\n", error->message);
			g_error_free(error);
			return 1;
		}

		g_printerr("An unknown error occurred\n");
		return 1;
	}

	g_option_context_free(context);

	initlog(debug_target);
	LOG("provisioning main");

	loop = g_main_loop_new(NULL, FALSE);

	exit_handler = NULL;
	msglist = NULL;

	dbus_error_init(&err);
	/* connect to the bus and check for errors. */
	conn = setup_dbus_bus(DBUS_BUS_SYSTEM, PROVISIONING_SERVICE, &err);
	if (conn == NULL) {
		if (dbus_error_is_set(&err) == TRUE) {
			LOG("Unable to hop onto D-Bus: %s",
					err.message);
			dbus_error_free(&err);
		} else {
			LOG("Unable to hop onto D-Bus");
		}

		goto cleanup;
	}

	dbus_provisioning_set_connection(conn);
	if(!provisioning_init())
		LOG("provisioning_init failed!");

	g_main_loop_run(loop);

	provisioning_cleanup();
	LOG("provisioning main exit");
	dbus_provisioning_set_connection(NULL);
	dbus_connection_unref(conn);
cleanup:
	g_main_loop_unref(loop);
	return 0;

}
