/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include "zfs_agents.h"
#include "../zed_log.h"


/*ARGSUSED*/
void
zfs_diagnosis_recv(nvlist_t *nvl, const char *class)
{
}

/*ARGSUSED*/
int
zfs_diagnosis_init(libzfs_handle_t *zfs_hdl)
{
	return (0);
}

/*ARGSUSED*/
void
zfs_diagnosis_fini(void)
{
}
