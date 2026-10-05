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
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef	_KSTAT_H
#define	_KSTAT_H

#include <sys/types.h>
#include "kstat_kernel.h"

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
