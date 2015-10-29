/*
 *  Copyright (C) 2014-2015 Jolla Ltd.
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

static guint exit_timeout_id;
static GMainLoop *loop;
static int pending_count;

static gboolean handle_exit(gpointer data)
{
	exit_timeout_id = 0;
	LOG("provisioning_exit");
	g_main_loop_quit(loop);
	return FALSE;
}

static void cancel_exit()
{
	if (exit_timeout_id) {
		g_source_remove(exit_timeout_id);
		exit_timeout_id = 0;
	}
}

static void schedule_exit()
{
	cancel_exit();
	if (!pending_count) {
		exit_timeout_id = g_timeout_add_seconds(2, handle_exit, NULL);
	}
}

static void send_signal(enum prov_result message)
{
	DBusConnection *conn = provisioning_dbus_get_connection();
	DBusMessage *msg;
	const char *name;

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

	LOG("send_signal %s", name);
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

static void provisioning_done(enum prov_result result, void *param)
{
	LOG("Provisioning result %d", result);
	send_signal(result);
	pending_count--;
	schedule_exit();
}

static gboolean handle_message(const char *imsi, const char *msg, int len)
{
	struct provisioning_data *prov_data;
	LOG("handle_message %s %d bytes", imsi, len);

	if (len < 0)
		goto error;

#ifdef FILEWRITE
	print_to_file(msg, len, "received_wbxml");
#endif

	prov_data = decode_provisioning_wbxml(msg, len);
	if (prov_data) {
		cancel_exit();
		pending_count++;
		provisioning_ofono(imsi, prov_data, provisioning_done, NULL);
	} else {
		send_signal(PROV_FAILURE);
		goto error;
	}

	LOG("provisioning_handle_message: SUCCESS");
	return TRUE;

error:
	schedule_exit();
	return FALSE;
}

static DBusMessage *provisioning_handle_message(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	char *array; /* Byte array containing client request*/
	int array_len; /* Length of request byte array */
	DBusMessageIter iter;
	DBusMessageIter subiter;
	const char *imsi = NULL;

	LOG("provisioning_handle_message");

	dbus_message_iter_init(msg, &iter);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
		goto error;

	dbus_message_iter_get_basic(&iter, &imsi);
	dbus_message_iter_next(&iter);

	/* Skip the parts we are not interested in */
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

	handle_message(imsi, array, array_len);
	return NULL;

error:
	schedule_exit();
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

	/* Exit in 30 seconds if nothing is happening */
	exit_timeout_id = g_timeout_add_seconds(30, handle_exit, NULL);
	g_main_loop_run(loop);

	provisioning_cleanup();
	LOG("provisioning main exit");
	dbus_provisioning_set_connection(NULL);
	dbus_connection_unref(conn);
cleanup:
	g_main_loop_unref(loop);
	return 0;

}

/*
 * Local Variables:
 * mode: C
 * tab-width: 4
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * End:
 */
