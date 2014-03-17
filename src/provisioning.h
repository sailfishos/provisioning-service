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
#ifndef __PROV_H
#define __PROV_H

#ifdef __cplusplus
extern "C" {
#endif

#define GDBUS_ARGS(args...) (const GDBusArgInfo[]) { args, { } }

#define GDBUS_METHOD(_name, _in_args, _out_args, _function) \
	.name = _name, \
	.in_args = _in_args, \
	.out_args = _out_args, \
	.function = _function, \

#define GDBUS_SIGNAL(_name, _args) \
	.name = _name, \
	.args = _args

enum prov_signal {
	PROV_SUCCESS = 0,
	PROV_PARTIAL_SUCCESS = 1,
	PROV_FAILURE = 2,
};

struct timeout_handler *exit_handler;

gboolean handle_exit(gpointer user_data);

void send_signal(guint message);

#ifdef __cplusplus
}
#endif

#endif /* __PROV_H */
