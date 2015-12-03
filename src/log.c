/*
 * Copyright (C) 2014-2015 Jolla Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include "log.h"
#include <gofono_types.h>
#include <stdlib.h>

GLOG_MODULE_DEFINE("provisioning");

/*
 * If no commandline target given reads log target from env setting.
 * May be called several times, supports dynamic re-configuration.
 *
 * call "$export set PROVISIONING_SERVICE_LOG=" with a value to activate logs.
 */
void initlog(int target)
{
	if (target <= 0) {
		const char *logarg = getenv("PROVISIONING_SERVICE_LOG");
		if (logarg) {
			target = atoi(logarg);
		}
	}
	if (target > 0) {
		GLOG_MODULE_NAME.level = GLOG_MODULE_NAME.max_level;
		gofono_log.level = gofono_log.max_level;
		switch (target) {
		case LOGJOURNAL:
			gutil_log_func = gutil_log_syslog;
			break;
		case LOGSTDOUT:
			gutil_log_func = gutil_log_stdout;
			break;
		case LOGSTDERR:
			gutil_log_func = gutil_log_stderr;
			break;
		}
	} else {
		GLOG_MODULE_NAME.level = GLOG_LEVEL_INFO;
		gofono_log.level = GLOG_LEVEL_ERR;
	}
}

/* Implementation of logging function */
void prov_debug(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	gutil_logv(GLOG_MODULE_CURRENT, GLOG_LEVEL_DEBUG, format, args);
	va_end(args);
}

/*
 * Local Variables:
 * mode: C
 * tab-width: 4
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * End:
 */
