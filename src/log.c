/*
* Copyright (C) 2014 Jolla Ltd.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
*/

#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"

#define JOURNAL "1"
#define STDOUT "2"
#define STDERR "3"
#define NONE 0x00
#define LOGJOURNAL 0x01
#define LOGSTDOUT 0x02
#define LOGSTDERR 0x03

static int logtarget = NONE;
static int log_priority = LOG_DEBUG;//LOG_ERR;
/*
 * If no commandline target given reads log target from env setting.
 * May be called several times, supports dynamic re-configuration.
 *
 * call "$export set PROVISIONING_SERVICE_LOG=" with a value to activate logs.
 */
extern void initlog(int target)
{
	char *logarg = NULL;

	if (target > 0)
		logtarget = target;
	else {
		logarg = getenv("PROVISIONING_SERVICE_LOG");

		if (logarg) {
			if (!strcmp(logarg, JOURNAL))
				logtarget = LOGJOURNAL;
			else if (!strcmp(logarg, STDOUT))
				logtarget = LOGSTDOUT;
			else if (!strcmp(logarg, STDERR))
				logtarget = LOGSTDERR;
		}
	}
}

/* Implementation of logging function */
extern void prov_debug(const char *format, ...)
{
	va_list args;

	/* Skip all function calls if no logging */
	if (logtarget == NONE)
		return;
	
	va_start(args, format);

	switch (logtarget) {
	case LOGJOURNAL:
		vsyslog(log_priority, format, args);
		break;
	case LOGSTDOUT:
		vfprintf(stdout, format , args);
		fprintf(stdout, "\n");
		break;
	case LOGSTDERR:
		vfprintf(stderr, format, args);
		fprintf(stderr, "\n");
		break;
	}

	va_end(args);
}
