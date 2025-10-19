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
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 */

#ifndef ZFS_CONTEXT_OS_H_
#define	ZFS_CONTEXT_OS_H_

#define	HAVE_LARGE_STACKS	1

extern char *vn_dumpdir;

extern uint64_t physmem;
extern const char *random_path;
extern const char *urandom_path;

/*
 * Hostname information
 */
typedef struct utsname	utsname_t;
extern utsname_t *utsname(void);

extern void kernel_init(int mode);
extern void kernel_fini(void);

struct spa;
extern void show_pool_stats(struct spa *);
extern int handle_tunable_option(const char *, boolean_t);

extern void kernel_init(int mode);
extern void kernel_fini(void);

#endif
