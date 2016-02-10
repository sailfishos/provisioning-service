/*
 *  Copyright (C) 2014-2016 Jolla Ltd.
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

#include "provisioning-decoder.h"
#include "provisioning-ofono.h"
#include "log.h"

#include <errno.h>

/* Generated code */
#include "org.nemomobile.provisioning.h"

#define PROVISIONING_SERVICE "org.nemomobile.provisioning"
#define PROVISIONING_SERVICE_INTERFACE "org.nemomobile.provisioning.interface"
#define PROVISIONING_SERVICE_PATH "/"
#define PROVISIONING_CONTENT_TYPE "application/vnd.wap.connectivity-wbxml"
#define PROVISIONING_BUS G_BUS_TYPE_SYSTEM

#ifndef PROV_MAX_SAVE_FILES
#  define PROV_MAX_SAVE_FILES (1000)
#endif

static guint exit_timeout_id;
static char *save_dir;
static GMainLoop *loop;
static GDBusConnection *dbus_connection;
static OrgNemomobileProvisioningInterface *provisioning_proxy;
static int pending_count;
static gulong handle_message_id;

static
gboolean
handle_exit(
	gpointer data)
{
	exit_timeout_id = 0;
	LOG("provisioning_exit");
	g_main_loop_quit(loop);
	return FALSE;
}

static
void
cancel_exit(void)
{
	if (exit_timeout_id) {
		g_source_remove(exit_timeout_id);
		exit_timeout_id = 0;
	}
}

static
void
schedule_exit(void)
{
	cancel_exit();
	if (!pending_count) {
		exit_timeout_id = g_timeout_add_seconds(2, handle_exit, NULL);
	}
}

static
void
provisioning_proxy_destroy(void)
{
	if (provisioning_proxy) {
        g_signal_handler_disconnect(provisioning_proxy, handle_message_id);
        g_dbus_interface_skeleton_unexport(
            G_DBUS_INTERFACE_SKELETON(provisioning_proxy));
		g_object_unref(provisioning_proxy);
		provisioning_proxy = NULL;
	}
}

static
void
send_signal(
	const char *imsi,
	const char *path,
	enum prov_result result)
{
	LOG("send_signal %s %d", imsi, result);
	if (provisioning_proxy) {
		if (!imsi) imsi = "";
		if (!path) path = "";
		switch (result) {
		case PROV_SUCCESS:
			org_nemomobile_provisioning_interface_emit_apn_provisioning_succeeded(
				provisioning_proxy, imsi, path);
			break;
		case PROV_PARTIAL_SUCCESS:
			org_nemomobile_provisioning_interface_emit_apn_provisioning_partially_succeeded(
				provisioning_proxy, imsi, path);
			break;
		default:
			org_nemomobile_provisioning_interface_emit_apn_provisioning_failed(
				provisioning_proxy, imsi, path);
			break;
		}
	}
}

static
void
provisioning_done(
	const char *imsi,
	const char *path,
	enum prov_result result,
	void *param)
{
	LOG("Provisioning result %d imsi %s path %s", result, imsi, path);
	send_signal(imsi, path, result);
	pending_count--;
	schedule_exit();
}

static
gboolean
handle_message(
	const char *imsi,
	const guint8 *msg,
	int len)
{
	struct provisioning_data *prov_data;

	LOG("handle_message %s %d bytes", imsi, len);

	if (save_dir && g_file_test(save_dir, G_FILE_TEST_IS_DIR)) {
		int i;
		GString *path = g_string_new(NULL);
		for (i=0; i<=PROV_MAX_SAVE_FILES; i++) {
			g_string_printf(path, "%s/provision_%04x.dat", save_dir, i);
			if (!g_file_test(path->str, G_FILE_TEST_EXISTS)) {
				GError *error = NULL;
				if (g_file_set_contents(path->str, (void*)msg, len, &error)) {
					GINFO("wrote %s", path->str);
				} else {
					GWARN("%s: %s", path->str, error->message);
					g_error_free(error);
				}
				break;
			}
		}
		g_string_free(path, TRUE);
	}

	prov_data = decode_provisioning_wbxml(msg, len);
	if (prov_data) {
		cancel_exit();
		pending_count++;
		provisioning_ofono(imsi, prov_data, provisioning_done, NULL);
		return TRUE;
	} else {
		return FALSE;
	}
}

static
gboolean
provisioning_handle_push_message(
	OrgNemomobileProvisioningInterface *proxy,
	GDBusMethodInvocation *call,
	const char *imsi,
	const char *from,
	guint32 remote_time,
	guint32 local_time,
	int dst_port,
	int src_port,
	const char *type,
	GVariant *data,
	void *user_data)
{
	gsize len = 0;
	const guint8* bytes = g_variant_get_fixed_array(data, &len, 1);
	if (!imsi || !imsi[0]) {
		GERR("Missing IMSI");
		g_dbus_method_invocation_return_error(call, G_DBUS_ERROR,
			G_DBUS_ERROR_FAILED, "Missing IMSI");
	} else if (!bytes || !len) {
		GERR("Missing provisioning data");
		g_dbus_method_invocation_return_error(call, G_DBUS_ERROR,
			G_DBUS_ERROR_FAILED, "Missing provisioning data");
    } else if (!type || !type[0]) {
        GERR("Missing content type");
		g_dbus_method_invocation_return_error(call, G_DBUS_ERROR,
			G_DBUS_ERROR_FAILED, "Missing content type");
    } else if (g_ascii_strcasecmp(type, PROVISIONING_CONTENT_TYPE)) {
        GERR("Unexpected content type %s", type);
		g_dbus_method_invocation_return_error(call, G_DBUS_ERROR,
			G_DBUS_ERROR_FAILED, "Unexpected content type");
	} else {
		if (!handle_message(imsi, bytes, len)) {
			send_signal(imsi, NULL, PROV_FAILURE);
			schedule_exit();
		}
		org_nemomobile_provisioning_interface_complete_handle_provisioning_message(proxy, call);
	}
    return TRUE;
}

static
void
provisioning_dbus_ready(
	GDBusConnection* bus,
	const gchar* name,
	gpointer arg)
{
	GError* error = NULL;
	LOG("Bus acquired");
	GASSERT(!provisioning_proxy);
	GASSERT(!dbus_connection);
	g_object_ref(dbus_connection = bus);
	provisioning_proxy = org_nemomobile_provisioning_interface_skeleton_new();
	if (g_dbus_interface_skeleton_export(
	    G_DBUS_INTERFACE_SKELETON(provisioning_proxy), bus,
	    PROVISIONING_SERVICE_PATH, &error)) {
		handle_message_id = g_signal_connect(provisioning_proxy,
			"handle-handle-provisioning-message",
			G_CALLBACK(provisioning_handle_push_message), NULL);
	} else {
		GERR("Could not start: %s", GERRMSG(error));
		g_error_free(error);
		g_object_unref(provisioning_proxy);
		provisioning_proxy = NULL;
	}
}

static
void
provisioning_dbus_name_acquired(
	GDBusConnection* bus,
	const gchar* name,
	gpointer arg)
{
	LOG("Acquired service name '%s'", name);
}

static
void
provisioning_dbus_name_lost(
	GDBusConnection* bus,
	const gchar* name,
	gpointer arg)
{
	GERR("'%s' service already running or access denied", name);
	g_main_loop_quit(loop);
}

static gint log_target = 0;
static gboolean debug = 0;

static GOptionEntry entries[] = {
	{ "log", 'l', 0,G_OPTION_ARG_INT, &log_target,
	  "Options: 1(system logger) 2(stdout) 3(stderr)", "1..3" },
	{ "timestamp", 't', 0, G_OPTION_ARG_NONE, &gutil_log_timestamp,
	  "Add timestamps to debug log", NULL },
	{ "debug", 'd', 0, G_OPTION_ARG_NONE, &debug,
	  "Disable start timeout for debugging", NULL },
	{ "save-dir", 's', 0, G_OPTION_ARG_STRING, &save_dir,
	  "Save received messages to DIR", "DIR" },
	{ NULL },
};

int main(int argc, char **argv)
{
	guint name_id;
	GError *error = NULL;
	GOptionContext *context = g_option_context_new(NULL);

	gutil_log_timestamp = FALSE;
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

	/* Turn logging on if debugging is enabled */
	if (debug && !log_target) log_target = LOGSTDOUT;
	initlog(log_target);
	LOG("Starting");

	/* Create file storage directory */
	if (save_dir) {
		if (g_mkdir_with_parents(save_dir, 0755) < 0) {
			GWARN("Error creating %s: %s", save_dir, strerror(errno));
		}
	}

	/* Acquire name, don't allow replacement */
	name_id = g_bus_own_name(PROVISIONING_BUS, PROVISIONING_SERVICE,
		G_BUS_NAME_OWNER_FLAGS_REPLACE, provisioning_dbus_ready,
		provisioning_dbus_name_acquired, provisioning_dbus_name_lost,
		NULL, NULL);

	loop = g_main_loop_new(NULL, FALSE);

	/* Exit in 30 seconds if nothing is happening */
	if (!debug) {
		exit_timeout_id = g_timeout_add_seconds(30, handle_exit, 0);
	}

	/* Run the main loop */
	g_main_loop_run(loop);
	g_main_loop_unref(loop);

	/* Cleanup */
	provisioning_proxy_destroy();
	g_bus_unown_name(name_id);
	if (dbus_connection) {
		g_dbus_connection_flush_sync(dbus_connection, NULL, NULL);
		g_object_unref(dbus_connection);
	}

	LOG("Exiting");
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
