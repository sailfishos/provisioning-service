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

#ifndef __PROVOFONO_H
#define __PROVOFONO_H

struct provisioning_data;

enum prov_result {
	PROV_SUCCESS = 0,
	PROV_PARTIAL_SUCCESS = 1,
	PROV_FAILURE = 2,
};

void
provisioning_ofono(
	const char *imsi,
	struct provisioning_data *data,
	void (*done)(enum prov_result result, void *param),
	void *param);

#endif /* __PROVOFONO_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 4
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * End:
 */
