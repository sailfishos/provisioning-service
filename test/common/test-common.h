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

#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include "log.h"

#define TEST_FLAG_DEBUG (0x01)

typedef struct test_opt {
	int flags;
} TestOpt;

/* Should be invoked after g_test_init */
void
test_init(
	TestOpt *opt,
	int argc,
	char *argv[]);

#endif /* TEST_COMMON_H */

/*
 * Local Variables:
 * mode: C
 * tab-width: 4
 * c-basic-offset: 4
 * indent-tabs-mode: t
 * End:
 */
