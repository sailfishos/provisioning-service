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
#include <string.h>
#include <stdio.h>
#include <glib.h>
#include <dbus/dbus.h>

#include "log.h"
#include "provisioning-receiver.h"
#include "provisioning.h"
#include "provisioning-ofono.h"
#include "provisioning-xml-parser.h"

#define OFONO_SERVICE		"org.ofono"
#define OFONO_MANAGER_INTERFACE	OFONO_SERVICE ".Manager"
#define OFONO_MODEM_INTERFACE	OFONO_SERVICE ".Modem"
#define OFONO_GPRS_INTERFACE	OFONO_SERVICE ".ConnectionManager"
#define OFONO_CONTEXT_INTERFACE	OFONO_SERVICE ".ConnectionContext"

struct modem_data {
	char *path;
	DBusConnection *conn;
	gboolean has_gprs;
	dbus_bool_t gprs_attached;
	struct ofono_internet *oi;
	struct ofono_mms *omms;
	struct ofono_temp *temp;
};

struct ofono_temp {
	char *context_path;
	dbus_bool_t context_active;
	char *type;
};

struct ofono_internet {
	char *context_path;
	dbus_bool_t context_active;
};

struct ofono_mms{
	char *context_path;
	dbus_bool_t context_active;
};

#ifdef MULTIMODEM
static GHashTable *modem_list;
#endif

struct modem_data *ofono_modem;

guint call_counter;
guint idle_id;
guint timer_id;
guint signal_prov;

static gboolean check_idle();
static void provisioning_internet(struct modem_data *modem, struct internet *net);
static void provisioning_mms(struct modem_data *modem, struct w4 *mms);
static void provisioning_w2(struct modem_data *modem, struct w2 *mms);
static void remove_modem();

static void clean_provisioning()
{
	LOG("clean_provisioning");

	if (idle_id > 0)
		g_source_remove(idle_id);

	if (timer_id > 0)
		g_source_remove(timer_id);

	send_signal(signal_prov);

	remove_modem();
	clean_provisioning_data();
	handle_exit(NULL);
}

static gboolean reset_idle_check()
{
	idle_id = g_idle_add(check_idle,NULL);
	return FALSE;
}

static gboolean check_idle()
{
	if (call_counter > 0) {
		timer_id = g_timeout_add_seconds(2, reset_idle_check, NULL);
		goto out;
	}

	clean_provisioning();
out:
	return FALSE;
}

static void set_context_property_reply(DBusPendingCall *call, void *user_data)
{
	DBusMessage *reply = dbus_pending_call_steal_reply(call);
	DBusError err;

	dbus_error_init(&err);

	if (signal_prov != PROV_PARTIAL_SUCCESS)
		signal_prov = PROV_SUCCESS;

	if (dbus_set_error_from_message(&err, reply) == TRUE) {
		LOG("set_context_property_reply:%s: %s",
			err.name, err.message);
		dbus_error_free(&err);
		signal_prov = PROV_PARTIAL_SUCCESS;
	}

	dbus_message_unref(reply);

	/*TODO: This could perhaps be done more elegantly but will do for now*/
	call_counter--;
	LOG("%d",call_counter);
}

static void deactivate_internet_context_reply(DBusPendingCall *call,
								void *user_data)
{
	DBusMessage *reply = dbus_pending_call_steal_reply(call);
	DBusError err;
	struct modem_data *modem;
	struct provisioning_data *prov_data;

	dbus_error_init(&err);

	if (dbus_set_error_from_message(&err, reply) == TRUE) {
		LOG("deactivate_internet_context_reply:%s: %s",
			err.name, err.message);
		dbus_error_free(&err);
		dbus_message_unref(reply);

		if (signal_prov != PROV_FAILURE)
			signal_prov = PROV_PARTIAL_SUCCESS;

		goto exit;
	}

	dbus_message_unref(reply);

	prov_data = get_provisioning_data();
	if (prov_data == NULL) {
		LOG("No provisioning data");

		if (signal_prov != PROV_FAILURE)
			signal_prov = PROV_PARTIAL_SUCCESS;

		clean_provisioning();
		return;
	}
	if (!prov_data->internet->apn && !prov_data->internet->apn) {
		LOG("No data to provisioning");

		if (signal_prov != PROV_FAILURE)
			signal_prov = PROV_PARTIAL_SUCCESS;

		clean_provisioning();
		return;
	}
	modem = ofono_modem;
	modem->oi->context_active = FALSE;

	if (prov_data->internet->apn)
		provisioning_internet(modem,prov_data->internet);

	if (prov_data->w2->apn)
		provisioning_w2(modem,prov_data->w2);

exit:
	/*TODO: This could perhaps be done more elegantly but will do for now*/
	call_counter--;
	LOG("%d",call_counter);
}

static void deactivate_mms_context_reply(DBusPendingCall *call, void *user_data)
{
	DBusMessage *reply = dbus_pending_call_steal_reply(call);
	DBusError err;
	struct modem_data *modem;
	struct provisioning_data *prov_data;

	dbus_error_init(&err);

	if (dbus_set_error_from_message(&err, reply) == TRUE) {
		LOG("deactivate_mms_context_reply:%s: %s",
			err.name, err.message);
		dbus_error_free(&err);
		dbus_message_unref(reply);

		if (signal_prov != PROV_FAILURE)
			signal_prov = PROV_PARTIAL_SUCCESS;

		goto exit;
	}

	dbus_message_unref(reply);

	prov_data = get_provisioning_data();
	if (prov_data == NULL) {
		LOG("No provisioning data");

		if (signal_prov != PROV_FAILURE)
			signal_prov = PROV_PARTIAL_SUCCESS;

		clean_provisioning();
		return;
	}
	if (!prov_data->internet->apn && !prov_data->w4->apn) {
		LOG("No data to provisioning");

		if (signal_prov != PROV_FAILURE)
			signal_prov = PROV_PARTIAL_SUCCESS;

		clean_provisioning();
		return;
	}
	modem = ofono_modem;
	modem->omms->context_active = FALSE;

	if (prov_data->w4->apn)
		provisioning_mms(modem,prov_data->w4);

exit:
	/*TODO: This could perhaps be done more elegantly but will do for now*/
	call_counter--;
	LOG("%d",call_counter);
}

static int set_context_property(struct modem_data *modem, const char *path,
				const char *property, const char *value,
								gboolean active)
{
	DBusConnection *conn = modem->conn;
	DBusMessage *msg;
	DBusMessageIter iter,val;
	DBusPendingCall *call;

	LOG("set_context_property");

	msg = dbus_message_new_method_call(OFONO_SERVICE, path,
					OFONO_CONTEXT_INTERFACE, "SetProperty");
	if (msg == NULL)
		return -ENOMEM;

	dbus_message_iter_init_append(msg, &iter);

	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &property);

	if (value) {
		dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT,
						DBUS_TYPE_STRING_AS_STRING,
						&val);

		dbus_message_iter_append_basic(&val, DBUS_TYPE_STRING, &value);
	} else {
		dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT,
						DBUS_TYPE_BOOLEAN_AS_STRING,
						&val);

		dbus_message_iter_append_basic(&val, DBUS_TYPE_BOOLEAN, &active);
	}

	dbus_message_iter_close_container(&iter, &val);

	if (dbus_connection_send_with_reply(conn, msg, &call, -1) == FALSE) {
		dbus_message_unref(msg);
		return -EIO;
	}

	dbus_message_unref(msg);

	if (call == NULL)
		return -EINVAL;

	if (g_strcmp0(property,"Active") == 0) {
		if (g_strcmp0(path,modem->oi->context_path) == 0)
			dbus_pending_call_set_notify(call,
					deactivate_internet_context_reply,
							(void *)value, NULL);
		if (g_strcmp0(path,modem->omms->context_path) == 0)
			dbus_pending_call_set_notify(call,
					deactivate_mms_context_reply,
							(void *)value, NULL);
	} else {
		dbus_pending_call_set_notify(call,
						set_context_property_reply,
							(void *)value, NULL);
	}

	dbus_pending_call_unref(call);
	call_counter++;
	LOG("%d",call_counter);
	return 0;
}

static void add_mms_context_reply(DBusPendingCall *call, void *user_data)
{
	DBusMessage *reply = dbus_pending_call_steal_reply(call);
	DBusError err;
	const char *path;
	struct modem_data *modem;
	struct provisioning_data *prov_data;

	LOG("add_mms_context_reply");
	dbus_error_init(&err);

	if (dbus_set_error_from_message(&err, reply) == TRUE) {
		LOG("add_mms_context_reply:%s: %s",
			err.name, err.message);
		dbus_error_free(&err);

		if (signal_prov != PROV_FAILURE)
			signal_prov = PROV_PARTIAL_SUCCESS;

	}

	if (!dbus_message_get_args (reply, NULL,
					DBUS_TYPE_OBJECT_PATH, &path,
					DBUS_TYPE_INVALID)) {

		if (signal_prov != PROV_FAILURE)
			signal_prov = PROV_PARTIAL_SUCCESS;

		goto exit;
	}

	modem = ofono_modem;
	modem->omms->context_path = g_strdup(path);
	modem->omms->context_active = FALSE;

	prov_data = get_provisioning_data();
	if (prov_data == NULL) {
		LOG("No provisioning data");

		if (signal_prov != PROV_FAILURE)
			signal_prov = PROV_PARTIAL_SUCCESS;

		clean_provisioning();
		return;
	}
	if (prov_data->w4->apn)
		provisioning_mms(modem,prov_data->w4);

exit:
	dbus_message_unref(reply);

	/*TODO: This could perhaps be done more elegantly but will do for now*/
	call_counter--;
	LOG("%d",call_counter);
}

static void add_internet_context_reply(DBusPendingCall *call, void *user_data)
{
	DBusMessage *reply = dbus_pending_call_steal_reply(call);
	DBusError err;
	const char *path;
	struct modem_data *modem;
	struct provisioning_data *prov_data;

	LOG("add_internet_context_reply");
	dbus_error_init(&err);

	if (dbus_set_error_from_message(&err, reply) == TRUE) {
		LOG("add_internet_context_reply:%s: %s",
			err.name, err.message);
		dbus_error_free(&err);

		if (signal_prov != PROV_FAILURE)
			signal_prov = PROV_PARTIAL_SUCCESS;

	}

	LOG("add_internet_context_reply2");
	if (!dbus_message_get_args (reply, NULL,
					DBUS_TYPE_OBJECT_PATH, &path,
					DBUS_TYPE_INVALID)) {

		if (signal_prov != PROV_FAILURE)
			signal_prov = PROV_PARTIAL_SUCCESS;

		goto exit;
	}

	modem = ofono_modem;
	modem->oi->context_path = g_strdup(path);
	modem->oi->context_active = FALSE;

	prov_data = get_provisioning_data();
	if (prov_data == NULL) {
		LOG("No provisioning data");

		if (signal_prov != PROV_FAILURE)
			signal_prov = PROV_PARTIAL_SUCCESS;

		clean_provisioning();
		return;
	}

	if (prov_data->internet->apn)
		provisioning_internet(modem,prov_data->internet);

	if (prov_data->w2->apn)
		provisioning_w2(modem,prov_data->w2);

exit:
	dbus_message_unref(reply);

	/*TODO: This could perhaps be done more elegantly but will do for now*/
	call_counter--;
	LOG("%d",call_counter);
}

static int add_context(struct modem_data *modem, const char *type)
{
	DBusConnection *conn = modem->conn;
	DBusMessage *msg;
	DBusPendingCall *call;

	LOG("add_context:%s",modem->path);

	msg = dbus_message_new_method_call(OFONO_SERVICE, modem->path,
					OFONO_GPRS_INTERFACE, "AddContext");

	if (msg == NULL)
		return -ENOMEM;

	dbus_message_append_args(msg, DBUS_TYPE_STRING, &type,DBUS_TYPE_INVALID);

	if (dbus_connection_send_with_reply(conn, msg, &call, -1) == FALSE) {
		dbus_message_unref(msg);
		return -EIO;
	}

	dbus_message_unref(msg);


	if (call == NULL)
		return -EINVAL;

	if (g_strcmp0(type,"mms") == 0)
		dbus_pending_call_set_notify(call, add_mms_context_reply, modem, NULL);

	if (g_strcmp0(type,"internet") == 0)
		dbus_pending_call_set_notify(call, add_internet_context_reply, modem, NULL);

	dbus_pending_call_unref(call);
	call_counter++;
	LOG("%d",call_counter);
	return 0;
}

static int create_context(struct modem_data *modem, const char *type)
{
	return add_context(modem, type);
}

static gboolean create_contexts(struct modem_data *modem)
{
	int ret1,ret2;
	struct provisioning_data *prov_data;
	prov_data = get_provisioning_data();
	ret1 = ret2 = 0;

	if (prov_data == NULL) {
		LOG("No provisioning data");
		clean_provisioning();
		return FALSE;
	}
	if (prov_data->internet->apn || prov_data->w2->apn)
		ret1 = create_context(modem, "internet");

	if (prov_data->w4->apn)
		ret2 = create_context(modem, "mms");

	if ((ret1 < 0) && (ret2 < 0))
		return FALSE;

	return TRUE;
}

static int deactivate_context(struct modem_data *modem, const char *path)
{
	return set_context_property(modem, path, "Active", NULL, FALSE);
}

static void set_w2_context_property(struct modem_data *modem, struct w2 *net)
{
	if (net->apn)
		set_context_property(modem, modem->oi->context_path,
					"AccessPointName", net->apn, FALSE);

	if (net->name)
		set_context_property(modem, modem->oi->context_path,
						"Name", net->name, FALSE);

	if (net->username)
		set_context_property(modem, modem->oi->context_path,
					"Username", net->username, FALSE);

	if (net->password)
		set_context_property(modem, modem->oi->context_path,
					"Password", net->password, FALSE);

}

static void provisioning_w2(struct modem_data *modem, struct w2 *net)
{

	LOG("provisioning_w2");

	if (!modem->oi->context_path) {
		LOG("set_provisioning:no internet context! create 1");
		create_context(modem, "internet");
		return;
	}

	if (modem->oi->context_active == TRUE) {
		LOG("provisioning_w2: context_active: deactivate it");
		deactivate_context(modem, modem->oi->context_path);
		return;
	}

	LOG("provisioning_w2:%s",modem->oi->context_path);
	LOG("provisioning_w2:%",net->apn);
	LOG("provisioning_w2:%",net->name);
	LOG("provisioning_w2:%",net->username);
	LOG("provisioning_w2:%",net->password);

	set_w2_context_property(modem,net);
}


static void set_internet_context_property(struct modem_data *modem,
							struct internet *net)
{
	if (net->apn)
		set_context_property(modem, modem->oi->context_path,
					"AccessPointName", net->apn, FALSE);

	if (net->name)
		set_context_property(modem, modem->oi->context_path,
					"Name", net->name, FALSE);

	if (net->username)
		set_context_property(modem, modem->oi->context_path,
					"Username", net->username, FALSE);

	if (net->password)
		set_context_property(modem, modem->oi->context_path,
					"Password", net->password, FALSE);

}

static void provisioning_internet(struct modem_data *modem,
							struct internet *net)
{

	LOG("provisioning_internet");

	if (!modem->oi->context_path) {
		LOG("set_provisioning:no internet context! create 1");
		create_context(modem, "internet");
		return;
	}

	if (modem->oi->context_active == TRUE) {
		LOG("provisioning_internet: context_active: deactivate it");
		deactivate_context(modem, modem->oi->context_path);
		return;
	}

	LOG("provisioning_internet:%s", modem->oi->context_path);
	LOG("provisioning_internet:%s", net->apn);
	LOG("provisioning_internet:%s", net->name);
	LOG("provisioning_internet:%s", net->username);
	LOG("provisioning_internet:%s", net->password);

	set_internet_context_property(modem,net);
}

static char *create_mms_proxy(struct w4 *mms)
{
	char *proxy;
	gsize len;

	LOG("mms->messageproxy:%s",mms->messageproxy);
	LOG("mms->portnro:%s",mms->portnro);
	proxy = NULL;

	if (mms->messageproxy && mms->portnro) {
		len = strlen(mms->messageproxy) + strlen(mms->portnro);
		LOG("%d",len);
		proxy = g_try_new(char, len + 2);
		snprintf(proxy, len + 2, "%s:%s",mms->messageproxy,mms->portnro);
	}

	return proxy;
}
static void set_mms_context_property(struct modem_data *modem, struct w4 *mms)
{

	char *mms_proxy;

	if (mms->apn)
		set_context_property(modem, modem->omms->context_path,
					"AccessPointName", mms->apn, FALSE);

	if (mms->name)
		set_context_property(modem, modem->omms->context_path,
					"Name", mms->name, FALSE);

	if (mms->username)
		set_context_property(modem, modem->omms->context_path,
					"Username", mms->username, FALSE);

	if (mms->password)
		set_context_property(modem, modem->omms->context_path,
					"Password", mms->password, FALSE);

	if (mms->messagecenter)
		set_context_property(modem, modem->omms->context_path,
					"MessageCenter", mms->messagecenter,
									FALSE);

	mms_proxy = create_mms_proxy(mms);
	if (mms_proxy)
		set_context_property(modem, modem->omms->context_path,
					"MessageProxy", mms_proxy, FALSE);

	g_free(mms_proxy);

}

static void provisioning_mms(struct modem_data *modem, struct w4 *mms)
{
	LOG("provisioning_mms");
	if (!modem->omms->context_path) {
		LOG("provisioning_mms:no mms context! create 1");
		create_context(modem, "mms");
		return;
	}

	if (modem->omms->context_active) {
		LOG("provisioning_mms: context_active: deactivate it");
		deactivate_context(modem, modem->omms->context_path);
		return;
	}

	LOG("provisioning_mms:%s",modem->omms->context_path);
	LOG("provisioning_mms:%s",mms->apn);
	LOG("provisioning_mms:%s",mms->name);
	LOG("provisioning_mms:%s",mms->username);
	LOG("provisioning_mms:%s",mms->password);
	LOG("provisioning_mms:%s",mms->messagecenter);

	set_mms_context_property(modem, mms);
}

static gboolean set_provisioning(struct modem_data *modem)
{
	struct provisioning_data *prov_data;

	LOG("set_provisioning");

	prov_data = get_provisioning_data();
	if (prov_data == NULL) {
		LOG("No provisioning data");
		return FALSE;
	}

	if (!prov_data->internet->apn && !prov_data->w4->apn
					&& !prov_data->w2->apn) {
		LOG("No provisioning data");
		return FALSE;
	}

	if (prov_data->internet->apn)
		provisioning_internet(modem,prov_data->internet);

	if (prov_data->w2->apn)
		provisioning_w2(modem,prov_data->w2);

	if (prov_data->w4->apn)
		provisioning_mms(modem, prov_data->w4);

	return TRUE;

}

static void create_local_context(struct modem_data *modem,
				const char *path, DBusMessageIter *iter)
{
	DBusMessageIter dict;

	LOG("create_local_context:%s",path);

	dbus_message_iter_recurse(iter, &dict);

	while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry, value;
		const char *key;

		dbus_message_iter_recurse(&dict, &entry);
		dbus_message_iter_get_basic(&entry, &key);
		LOG("create_local_context:%s",key);
		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &value);

		if (g_str_equal(key, "Type") == TRUE) {
			const char *type;

			dbus_message_iter_get_basic(&value, &type);
			LOG("type:%s",type);

			modem->temp->type =g_strdup(type);
			modem->temp->context_path = g_strdup(path);

		} else if (g_str_equal(key, "Active") == TRUE) {

			dbus_bool_t active;

			dbus_message_iter_get_basic(&value, &active);

			modem->temp->context_active = active;

			LOG("Context active %d", modem->temp->context_active);
		}
		dbus_message_iter_next(&dict);
	}
	/*
	 * if need for more than 1 internet and mms remove && check from
	 * following statements and use list in modem struct
	 */
	if (!g_strcmp0(modem->temp->type, "internet") &&
			(modem->oi->context_path == NULL)) {
		modem->oi->context_path = modem->temp->context_path;
		modem->oi->context_active = modem->temp->context_active;
		LOG("type:%s,oi->context_path:%s,oi->context_active:%d",
				modem->temp->type,modem->oi->context_path,
				modem->oi->context_active);
	}

	if (!g_strcmp0(modem->temp->type, "mms") &&
			(modem->omms->context_path == NULL)) {
		modem->omms->context_path = modem->temp->context_path;
		modem->omms->context_active = modem->temp->context_active;
		LOG("type:%s,omms->context_path:%s,omms->context_active:%d",
				modem->temp->type,modem->omms->context_path,
				modem->omms->context_active);
	}

	modem->temp->context_path = NULL;
	modem->temp->context_active = FALSE;
}

static gboolean create_local_contexts(struct modem_data *modem,
					DBusMessageIter list)
{
	gboolean ret = FALSE;

	LOG("create_local_contexts");

	while (dbus_message_iter_get_arg_type(&list) == DBUS_TYPE_STRUCT) {
		DBusMessageIter entry;
		const char *path;
		dbus_message_iter_recurse(&list, &entry);
		dbus_message_iter_get_basic(&entry, &path);

		dbus_message_iter_next(&entry);

		create_local_context(modem, path, &entry);

	/*
	 * Currently we look only for 1st internet and 1st mms and use those if
	 * need to provision more create list of internet and mms contexts
	 * + if needed recognice wap and ims and remove the following check.
	 */
		if ((modem->oi->context_path != NULL) &&
				(modem->omms->context_path != NULL)) {
			break;
		}

		dbus_message_iter_next(&list);
	}

	if ((modem->oi->context_path != NULL) ||
		(modem->omms->context_path != NULL))
		ret = TRUE;

	return ret;
}


static void get_contexts_reply(DBusPendingCall *call, void *user_data)
{
	struct modem_data *modem = user_data;
	DBusMessage *reply = dbus_pending_call_steal_reply(call);
	DBusMessageIter iter, list;
	DBusError err;

	dbus_error_init(&err);

	LOG("get_contexts_reply");

	if (dbus_set_error_from_message(&err, reply) == TRUE) {
		LOG("get_contexts_reply:%s: %s",
			err.name, err.message);
		dbus_error_free(&err);
		goto done;
	}

	if (dbus_message_has_signature(reply, "a(oa{sv})") == FALSE)
		goto done;

	if (dbus_message_iter_init(reply, &iter) == FALSE)
		goto done;

	dbus_message_iter_recurse(&iter, &list);

	call_counter--;
	LOG("%d",call_counter);

	if (create_local_contexts(modem, list)) {
		if (!set_provisioning(modem))
			goto done;

		dbus_message_unref(reply);
		return;
	} else {
		if (!create_contexts(modem))
			goto done;

		dbus_message_unref(reply);
		return;
	}
done:
	dbus_message_unref(reply);
	clean_provisioning();
}

static int get_contexts(struct modem_data *modem)
{
	DBusConnection *conn = modem->conn;
	DBusMessage *msg;
	DBusPendingCall *call;

	LOG("get_contexts");
	msg = dbus_message_new_method_call(OFONO_SERVICE, modem->path,
					OFONO_GPRS_INTERFACE, "GetContexts");
	if (msg == NULL)
		return -ENOMEM;

	if (dbus_connection_send_with_reply(conn, msg, &call, -1) == FALSE) {
		dbus_message_unref(msg);
		return -EIO;
	}

	dbus_message_unref(msg);

	if (call == NULL)
		return -EINVAL;

	dbus_pending_call_set_notify(call, get_contexts_reply, modem, NULL);

	dbus_pending_call_unref(call);

	call_counter++;
	LOG("%d",call_counter);
	return 0;
}


static void check_gprs_attached(struct modem_data *modem, DBusMessageIter *iter)
{
	dbus_bool_t attached;

	dbus_message_iter_get_basic(iter, &attached);

	modem->gprs_attached = attached;

	LOG("GPRS attached %d", modem->gprs_attached);

}

static void get_gprs_properties_reply(DBusPendingCall *call, void *user_data)
{
	struct modem_data *modem = user_data;
	DBusMessage *reply = dbus_pending_call_steal_reply(call);
	DBusMessageIter iter, dict;
	DBusError err;

	dbus_error_init(&err);

	if (dbus_set_error_from_message(&err, reply) == TRUE) {
		LOG("get_gprs_properties_reply:%s: %s",
			err.name, err.message);
		dbus_error_free(&err);
		goto done;
	}

	if (dbus_message_has_signature(reply, "a{sv}") == FALSE)
		goto done;

	if (dbus_message_iter_init(reply, &iter) == FALSE)
		goto done;

	dbus_message_iter_recurse(&iter, &dict);

	while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry, value;
		const char *key;

		dbus_message_iter_recurse(&dict, &entry);

		dbus_message_iter_get_basic(&entry, &key);
		dbus_message_iter_next(&entry);

		dbus_message_iter_recurse(&entry, &value);

		if (g_str_equal(key, "Attached") == TRUE)
			check_gprs_attached(modem, &value);

		dbus_message_iter_next(&dict);
	}

done:
	dbus_message_unref(reply);
	call_counter--;
	LOG("%d",call_counter);
	if (modem->gprs_attached) {
		if (get_contexts(modem) < 0)
			clean_provisioning();
	} else
		clean_provisioning();
}
static int get_gprs_properties(struct modem_data *modem)
{
	DBusConnection *conn = modem->conn;
	DBusMessage *msg;
	DBusPendingCall *call;
	LOG("get_gprs_properties");
	msg = dbus_message_new_method_call(OFONO_SERVICE, modem->path,
					OFONO_GPRS_INTERFACE, "GetProperties");
	if (msg == NULL)
		return -ENOMEM;

	if (dbus_connection_send_with_reply(conn, msg, &call, -1) == FALSE) {
		dbus_message_unref(msg);
		return -EIO;
	}

	dbus_message_unref(msg);

	if (call == NULL)
		return -EINVAL;

	dbus_pending_call_set_notify(call, get_gprs_properties_reply,
							modem, NULL);

	dbus_pending_call_unref(call);


	call_counter++;
	LOG("%d",call_counter);
	return 0;
}

static void check_interfaces(struct modem_data *modem, DBusMessageIter *iter)
{
	DBusMessageIter entry;
	gboolean has_gprs = FALSE;
	LOG("check_interfaces");
	dbus_message_iter_recurse(iter, &entry);

	while (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
		const char *interface;

		dbus_message_iter_get_basic(&entry, &interface);

		if (g_str_equal(interface, OFONO_GPRS_INTERFACE) == TRUE)
			has_gprs = TRUE;

		dbus_message_iter_next(&entry);
	}

	/*TODO: If no gprs should we wait a while and re-try once? */
	if (has_gprs) {
		if (get_gprs_properties(modem) < 0)
			clean_provisioning();
	} else
		clean_provisioning();

}
static void remove_modem()
{
	struct modem_data *modem = ofono_modem;

	if (modem != NULL) {
		LOG("remove_modem");
		dbus_connection_unref(modem->conn);

		g_free(modem->temp->context_path);
		g_free(modem->temp->type);
		g_free(modem->temp);

		g_free(modem->oi->context_path);
		g_free(modem->oi);

		g_free(modem->omms->context_path);
		g_free(modem->omms);

		g_free(modem->path);
		g_free(modem);
	}
}

static void create_modem(DBusConnection *conn, const char *path,
							DBusMessageIter *iter)
{
	struct modem_data *modem;
	struct ofono_internet *oi;
	struct ofono_mms *omms;
	struct ofono_temp *temp;
	DBusMessageIter dict;

	LOG("create_modem");

	modem = g_try_new0(struct modem_data, 1);
	if (modem == NULL) {
		clean_provisioning();
		return;
	}

	modem->path = g_strdup(path);
	modem->conn = dbus_connection_ref(conn);

	modem->has_gprs = FALSE;

	temp = g_try_new0(struct ofono_temp, 1);
	if (temp == NULL) {
		clean_provisioning();
		return;
	}
	temp->context_active = FALSE;
	temp->context_path = NULL;
	temp->type = NULL;
	modem->temp = temp;

	oi = g_try_new0(struct ofono_internet, 1);
	if (oi == NULL) {
		clean_provisioning();
		return;
	}

	oi->context_active = FALSE;
	oi->context_path = NULL;
	modem->oi = oi;

	omms = g_try_new0(struct ofono_mms, 1);
	if (omms == NULL) {
		clean_provisioning();
		return;
	}

	omms->context_active = FALSE;
	omms->context_path = NULL;
	modem->omms = omms;

	LOG("path %s", modem->path);

#ifdef MULTIMODEM
	g_hash_table_replace(modem_list, modem->path, modem);
#endif
	ofono_modem = modem;
	dbus_message_iter_recurse(iter, &dict);

	while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry, value;
		const char *key;

		dbus_message_iter_recurse(&dict, &entry);
		dbus_message_iter_get_basic(&entry, &key);

		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &value);

		if (g_str_equal(key, "Interfaces") == TRUE)
			check_interfaces(modem, &value);

		dbus_message_iter_next(&dict);
	}
}

static void get_modems_reply(DBusPendingCall *call, void *user_data)
{
	DBusConnection *conn = user_data;
	DBusMessage *reply = dbus_pending_call_steal_reply(call);
	DBusMessageIter iter, list;
	DBusError err;
	LOG("get_modems_reply");
	dbus_error_init(&err);

	if (dbus_set_error_from_message(&err, reply) == TRUE) {
		LOG("get_modems_reply:%s: %s",
			err.name, err.message);
		dbus_error_free(&err);
		goto done;
	}

	if (dbus_message_has_signature(reply, "a(oa{sv})") == FALSE)
		goto done;

	if (dbus_message_iter_init(reply, &iter) == FALSE)
		goto done;

	dbus_message_iter_recurse(&iter, &list);

	while (dbus_message_iter_get_arg_type(&list) == DBUS_TYPE_STRUCT) {
		DBusMessageIter entry;
		const char *path;

		dbus_message_iter_recurse(&list, &entry);
		dbus_message_iter_get_basic(&entry, &path);

		dbus_message_iter_next(&entry);

		create_modem(conn, path, &entry);

		dbus_message_iter_next(&list);
	}

	dbus_message_unref(reply);
	call_counter--;
	LOG("get_modems_reply:%d",call_counter);
	return;

done:
	dbus_message_unref(reply);
	clean_provisioning();
}
static int get_modems(DBusConnection *conn)
{
	DBusMessage *msg;
	DBusPendingCall *call;
	LOG("get_modems");
	msg = dbus_message_new_method_call(OFONO_SERVICE, "/",
					OFONO_MANAGER_INTERFACE, "GetModems");
	if (msg == NULL)
		return -ENOMEM;

	if (dbus_connection_send_with_reply(conn, msg, &call, -1) == FALSE) {
		dbus_message_unref(msg);
		return -EIO;
	}

	dbus_message_unref(msg);

	if (call == NULL)
		return -EINVAL;

	dbus_pending_call_set_notify(call, get_modems_reply, conn, NULL);

	dbus_pending_call_unref(call);

	call_counter++;
	LOG("%d",call_counter);
	idle_id = g_idle_add(check_idle,NULL);

	return 0;
}

static DBusConnection *connection;

int provisioning_init_ofono(void)
{
	int ret;

	LOG("provisioning_init_ofono");
	call_counter = 0;
	idle_id = 0;
	ret = 0;
	ofono_modem = NULL;
	signal_prov = PROV_FAILURE;

	connection = setup_dbus_bus(DBUS_BUS_SYSTEM, NULL, NULL);
	if (connection == NULL) {
		ret = -EPERM;
		goto exit;
	}
/*
 * Currently this assumes that only one modem in ofono if need to support more
 * it could be something like below
 */
#ifdef MULTIMODEM
	modem_list = g_hash_table_new_full(g_str_hash, g_str_equal,
						NULL, remove_modem);
#endif
	ret = get_modems(connection);
	if (ret < 0)
		goto exit;

exit:
	return ret;
}

void provisioning_exit_ofono(void)
{
	clean_provisioning();
}
