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
#ifndef __PROVXMLPARSER_H
#define __PROVXMLPARSER_H

struct provisioning_data {
	struct provisioning_internet *internet;
	struct provisioning_mms *mms;
};

struct provisioning_internet {
	char *name;
	char *apn;
	char *username;
	char *password;
};

struct provisioning_mms {
	char *name;
	char *apn;
	char *username;
	char *password;
	char *messageproxy;
	char *messagecenter;
	char *portnro;
};

struct provisioning_data *parse_xml_main(const char *xml, int len);
void provisioning_data_free(struct provisioning_data *data);

#endif /* __PROVXMLPARSER_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 4
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * End:
 */
