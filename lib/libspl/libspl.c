// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
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
