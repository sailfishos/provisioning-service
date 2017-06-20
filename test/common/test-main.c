/*
 *  Copyright (C) 2017 Jolla Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include "test-common.h"

#include <gutil_log.h>

GLOG_MODULE_DEFINE("provisioning");

void
prov_debug(
	const char *format, ...)
{
	va_list args;
	va_start(args, format);
	gutil_logv(GLOG_MODULE_CURRENT, GLOG_LEVEL_DEBUG, format, args);
	va_end(args);
}

void
test_init(
	TestOpt *opt,
	int argc,
	char *argv[])
{
	int i;
	memset(opt, 0, sizeof(*opt));
	for (i=1; i<argc; i++) {
		const char *arg = argv[i];
		if (!strcmp(arg, "-d") || !strcmp(arg, "--debug")) {
			opt->flags |= TEST_FLAG_DEBUG;
		} else if (!strcmp(arg, "-v")) {
			GTestConfig* config = (GTestConfig*)g_test_config_vars;
			config->test_verbose = TRUE;
		} else {
			GWARN("Unsupported command line option %s", arg);
		}
	}

	/* Setup logging */
	gutil_log_timestamp = FALSE;
	gutil_log_default.level = g_test_verbose() ? GLOG_LEVEL_VERBOSE :
		GLOG_LEVEL_NONE;
}

/*
 * Local Variables:
 * mode: C
 * tab-width: 4
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * End:
 */
