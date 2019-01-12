/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source. A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2017, 2018 by Delphix. All rights reserved.
 */

#ifndef _SYS_ZTHR_H
#define	_SYS_ZTHR_H

typedef struct zthr zthr_t;
typedef void (zthr_func_t)(void *, zthr_t *);
typedef boolean_t (zthr_checkfunc_t)(void *, zthr_t *);

extern zthr_t *zthr_create(zthr_checkfunc_t checkfunc,
    zthr_func_t *func, void *arg);
extern zthr_t *zthr_create_timer(zthr_checkfunc_t *checkfunc,
    zthr_func_t *func, void *arg, hrtime_t nano_wait);
extern void zthr_destroy(zthr_t *t);

extern void zthr_wakeup(zthr_t *t);
extern void zthr_cancel(zthr_t *t);
extern void zthr_resume(zthr_t *t);

extern boolean_t zthr_iscancelled(zthr_t *t);

#endif /* _SYS_ZTHR_H */
