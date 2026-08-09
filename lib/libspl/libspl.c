// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2016 Actifio, Inc. All rights reserved.
 * Copyright (c) 2025, Klara, Inc.
 * Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
 */

#include <libspl.h>
#include <assert.h>
#include <unistd.h>
#include <sys/misc.h>
#include <sys/systm.h>
#include <sys/utsname.h>
#include "libspl_impl.h"

static uint64_t hw_physmem = 0;
static struct utsname hw_utsname = {};

uint64_t
libspl_physmem(void)
{
	return (hw_physmem);
}

utsname_t *
utsname(void)
{
	return (&hw_utsname);
}

void
libspl_init(void)
{
	hw_physmem = sysconf(_SC_PHYS_PAGES);

	VERIFY0(uname(&hw_utsname));

	random_init();
}

void
libspl_fini(void)
{
	random_fini();
}
