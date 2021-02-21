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

#ifndef _SPL_ZONE_H
#define	_SPL_ZONE_H

#include <sys/byteorder.h>
#include <sys/cred.h>

#include <linux/cred.h>
#include <linux/user_namespace.h>

/*
 * Attach the given dataset to the given user namespace.
 */
extern int zone_dataset_attach(cred_t *, const char *, int);

/*
 * Detach the given dataset from the given user namespace.
 */
extern int zone_dataset_detach(cred_t *, const char *, int);

/*
 * Returns true if the named pool/dataset is visible in the current zone.
 */
extern int zone_dataset_visible(const char *dataset, int *write);

int spl_zone_init(void);
void spl_zone_fini(void);

extern unsigned int crgetzoneid(const cred_t *);
extern unsigned int global_zoneid(void);
extern boolean_t inglobalzone(proc_t *);

#define	INGLOBALZONE(x) inglobalzone(x)
#define	GLOBAL_ZONEID	global_zoneid()

#endif /* SPL_ZONE_H */
