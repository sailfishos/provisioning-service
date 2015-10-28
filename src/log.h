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

#ifndef __PROVSERVICELOG_H
#define __PROVSERVICELOG_H

#define GLOG_MODULE_NAME provisioning_log

#include <gutil_log.h>

/* Initializes logger */
extern void initlog(int target);

/* Prototype for log implementation function */
extern void prov_debug(const char *format, ...);

#define LOG(fmt, args...) { \
		prov_debug(fmt, ## args); \
}

#endif /*__PROVSERVICELOG_H */
