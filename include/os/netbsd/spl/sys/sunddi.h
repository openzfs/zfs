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
 * Minimal DDI bits used by OpenZFS on NetBSD (string convert, copyin).
 */

#ifndef _SPL_SUNDDI_H
#define	_SPL_SUNDDI_H

#include <sys/cred.h>
#include <sys/uio.h>
#include <sys/mutex.h>
#include <sys/u8_textprep.h>
#ifdef BUILDING_ZFS
#include <sys/vnode.h>
#endif

typedef int ddi_devid_t;

#define	DDI_DEV_T_NONE				((dev_t)-1)
#define	DDI_DEV_T_ANY				((dev_t)-2)
#define	DI_MAJOR_T_UNKNOWN			((major_t)0)

#define	DDI_PROP_DONTPASS			0x0001
#define	DDI_PROP_CANSLEEP			0x0002

#define	DDI_SUCCESS				0
#define	DDI_FAILURE				-1

#define	ddi_prop_lookup_string(x1, x2, x3, x4, x5)	(*x5 = NULL)
#define	ddi_prop_free(x)				(void)0
#define	ddi_root_node()					(void)0

extern int ddi_strtol(const char *, char **, int, long *);
extern int ddi_strtoul(const char *, char **, int, unsigned long *);
extern int ddi_strtoull(const char *, char **, int, unsigned long long *);
extern int ddi_strtoll(const char *, char **, int, longlong_t *);
extern int ddi_ffs(long);
extern int ddi_fls(long);


extern int ddi_copyin(const void *from, void *to, size_t len, int flags);
extern int ddi_copyout(const void *from, void *to, size_t len, int flags);
extern void ddi_sysevent_init(void);


int ddi_soft_state_init(void **statep, size_t size, size_t nitems);
void ddi_soft_state_fini(void **statep);

void *ddi_get_soft_state(void *state, int item);
int ddi_soft_state_zalloc(void *state, int item);
void ddi_soft_state_free(void *state, int item);

#endif /* SPL_SUNDDI_H */
