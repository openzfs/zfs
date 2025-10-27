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

