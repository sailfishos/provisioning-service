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
#ifndef __PROVSERVICEDECODER_H
#define __PROVSERVICEDECODER_H

#include <glib.h>

enum prov_authtype {
	AUTH_UNKNOWN = 0,
	AUTH_PAP,
	AUTH_CHAP,
	AUTH_MD5
};

struct provisioning_data {
	struct provisioning_internet *internet;
	struct provisioning_mms *mms;
};

struct provisioning_internet {
	char *name;
	char *apn;
	char *username;
	char *password;
	enum prov_authtype authtype;
};

struct provisioning_mms {
	char *name;
	char *apn;
	char *username;
	char *password;
	char *messageproxy;
	char *messagecenter;
	char *portnro;
	enum prov_authtype authtype;
};

struct provisioning_data *
decode_provisioning_wbxml(
	const guint8 *bytes,
	int len);

void
provisioning_data_free(
	struct provisioning_data *data);

#endif /* __PROVSERVICEDECODER_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 4
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * End:
 */
