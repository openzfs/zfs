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
 */

#include <sys/cred.h>

uid_t
crgetuid(cred_t *cr)
{
	(void) cr;
	return (0);
}

uid_t
crgetruid(cred_t *cr)
{
	(void) cr;
	return (0);
}

gid_t
crgetgid(cred_t *cr)
{
	(void) cr;
	return (0);
}

int
crgetngroups(cred_t *cr)
{
	(void) cr;
	return (0);
}

gid_t *
crgetgroups(cred_t *cr)
{
	(void) cr;
	return (NULL);
}
