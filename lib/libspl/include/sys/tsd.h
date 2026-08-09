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
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 */

#ifndef _SYS_TSD_H
#define	_SYS_TSD_H

#include <pthread.h>

/*
 * Thread-specific data
 */
#define	tsd_get(k)		pthread_getspecific(k)
#define	tsd_set(k, v)		pthread_setspecific(k, v)
#define	tsd_create(kp, d)	pthread_key_create((pthread_key_t *)kp, d)
#define	tsd_destroy(kp)		/* nothing */

#endif /* _SYS_MUTEX_H */
