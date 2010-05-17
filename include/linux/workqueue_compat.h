/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://github.com/behlendorf/spl/>.
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
\*****************************************************************************/

#ifndef _SPL_WORKQUEUE_COMPAT_H
#define _SPL_WORKQUEUE_COMPAT_H

#include <linux/workqueue.h>
#include <sys/types.h>

#ifdef HAVE_3ARGS_INIT_WORK

#define delayed_work			work_struct

#define spl_init_work(wq, cb, d)	INIT_WORK((wq), (void *)(cb), \
						  (void *)(d))
#define spl_init_delayed_work(wq,cb,d)	INIT_WORK((wq), (void *)(cb), \
						  (void *)(d))
#define spl_get_work_data(d, t, f)	(t *)(d)

#else

#define spl_init_work(wq, cb, d)	INIT_WORK((wq), (void *)(cb));
#define spl_init_delayed_work(wq,cb,d)	INIT_DELAYED_WORK((wq), (void *)(cb));
#define spl_get_work_data(d, t, f)	(t *)container_of(d, t, f)

#endif /* HAVE_3ARGS_INIT_WORK */

#endif  /* _SPL_WORKQUEUE_COMPAT_H */
