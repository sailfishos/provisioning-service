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
#ifndef __PROVSERVICERECEIVER_H
#define __PROVSERVICERECEIVER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef DBusMessage * (* GDBusMethodFunction) (DBusConnection *connection,
					DBusMessage *message, void *user_data);

typedef gboolean (* GDBusSignalFunction) (DBusConnection *connection,
					DBusMessage *message, void *user_data);

typedef struct GDBusMethodTable GDBusMethodTable;
typedef struct GDBusSignalTable GDBusSignalTable;

typedef void (* GDBusDestroyFunction) (void *user_data);

typedef struct GDBusArgInfo GDBusArgInfo;

struct GDBusArgInfo {
	const char *name;
	const char *signature;
};

struct GDBusMethodTable {
	const char *name;
	GDBusMethodFunction function;
	const GDBusArgInfo *in_args;
	const GDBusArgInfo *out_args;
};

struct GDBusSignalTable {
	const char *name;
	const GDBusArgInfo *args;
};

struct timeout_handler {
	guint id;
	DBusTimeout *timeout;
};

DBusConnection *setup_dbus_bus(DBusBusType type, const char *name, DBusError *error);

void dbus_provisioning_set_connection(DBusConnection *conn);

DBusConnection *provisioning_dbus_get_connection(void);

gboolean register_dbus_interface(DBusConnection *connection,
					const char *path, const char *name,
					const GDBusMethodTable *methods,
					const GDBusSignalTable *signal,
					void *user_data,
					GDBusDestroyFunction destroy);

gboolean unregister_dbus_interface(DBusConnection *connection,
					const char *path, const char *name);
#ifdef __cplusplus
}
#endif

#endif /* __PROVSERVICERECEIVER_H */


