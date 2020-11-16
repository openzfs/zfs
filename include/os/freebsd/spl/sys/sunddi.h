/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
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

extern int ddi_strtoul(const char *, char **, int, unsigned long *);
extern int ddi_strtol(const char *, char **, int, long *);
extern int ddi_strtoull(const char *, char **, int, unsigned long long *);
extern int ddi_strtoll(const char *, char **, int, long long *);

extern int ddi_copyin(const void *from, void *to, size_t len, int flags);
extern int ddi_copyout(const void *from, void *to, size_t len, int flags);
extern void ddi_sysevent_init(void);


int ddi_soft_state_init(void **statep, size_t size, size_t nitems);
void ddi_soft_state_fini(void **statep);

void *ddi_get_soft_state(void *state, int item);
int ddi_soft_state_zalloc(void *state, int item);
void ddi_soft_state_free(void *state, int item);

#endif /* SPL_SUNDDI_H */
