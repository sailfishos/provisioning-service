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

struct provisioning_data *
decode_provisioning_wbxml(
	const guint8 *array,
	int array_len);

#ifdef FILEWRITE
/*used for testing*/
void print_to_file(const void *data, int len, const char *file);
#endif

#endif /* __PROVSERVICEDECODER_H */
