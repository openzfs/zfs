/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef	_KSTAT_H
#define	_KSTAT_H

#include <sys/types.h>
#include <sys/kstat.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * kstat_open() returns a pointer to a kstat_ctl_t.
 * This is used for subsequent libkstat operations.
 */
typedef struct kstat_ctl {
	kid_t	kc_chain_id;	/* current kstat chain ID	*/
	kstat_t	*kc_chain;	/* pointer to kstat chain	*/
	HANDLE	kc_kd;		/* libzfs_handle to /dev/zfs descriptor	*/
} kstat_ctl_t;

extern	kstat_ctl_t	*kstat_open(void);
extern	int		kstat_close(kstat_ctl_t *);
extern	kid_t		kstat_read(kstat_ctl_t *, kstat_t *, void *);
extern	kid_t		kstat_write(kstat_ctl_t *, kstat_t *, void *);
extern	kid_t		kstat_chain_update(kstat_ctl_t *);
extern	kstat_t		*kstat_lookup(kstat_ctl_t *, char *, int, char *);
extern	void		*kstat_data_lookup(kstat_t *, char *);

extern int gmatch(const char *s, const char *p);

#ifdef	__cplusplus
}
#endif

#endif	/* _KSTAT_H */
