/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License Version 1.0 (CDDL-1.0).
 * You can obtain a copy of the license from the top-level file
 * "OPENSOLARIS.LICENSE" or at <http://opensource.org/licenses/CDDL-1.0>.
 * You may not use this file except in compliance with the license.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2016, Intel Corporation.
 */

#ifndef	ZED_DISK_EVENT_H
#define	ZED_DISK_EVENT_H

#ifdef	__cplusplus
extern "C" {
#endif

extern int zed_disk_event_init(void);
extern void zed_disk_event_fini(void);

#ifdef	__cplusplus
}
#endif

#endif	/* !ZED_DISK_EVENT_H */
