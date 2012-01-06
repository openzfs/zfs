/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
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

#ifndef _SPL_DEVICE_H
#define _SPL_DEVICE_H

#include <linux/device.h>

/*
 * Preferred API from 2.6.18 to 2.6.26+
 */
#ifdef HAVE_DEVICE_CREATE

typedef struct class			spl_class;
typedef struct device			spl_device;

#define spl_class_create(mod, name)	class_create(mod, name)
#define spl_class_destroy(cls)		class_destroy(cls)

# ifdef HAVE_5ARGS_DEVICE_CREATE
# define spl_device_create(cls, parent, devt, drvdata, fmt, args...)          \
	device_create(cls, parent, devt, drvdata, fmt, ## args)
# else
# define spl_device_create(cls, parent, devt, drvdata, fmt, args...)          \
	device_create(cls, parent, devt, fmt, ## args)
# endif

#define spl_device_destroy(cls, cls_dev, devt)                                \
	device_destroy(cls, devt)

/*
 * Preferred API from 2.6.13 to 2.6.17
 * Depricated in 2.6.18
 * Removed in 2.6.26
 */
#else
#ifdef HAVE_CLASS_DEVICE_CREATE

typedef struct class			spl_class;
typedef struct class_device		spl_device;

#define spl_class_create(mod, name)	class_create(mod, name)
#define spl_class_destroy(cls)		class_destroy(cls)
#define spl_device_create(cls, parent, devt, device, fmt, args...)            \
	class_device_create(cls, parent, devt, device, fmt, ## args)
#define spl_device_destroy(cls, cls_dev, devt)                                \
	class_device_unregister(cls_dev)

/*
 * Prefered API from 2.6.0 to 2.6.12
 * Depricated in 2.6.13
 * Removed in 2.6.13
 */
#else /* Legacy API */

typedef struct class_simple		spl_class;
typedef struct class_device		spl_class_device;

#define spl_class_create(mod, name)	class_simple_create(mod, name)
#define spl_class_destroy(cls)		class_simple_destroy(cls)
#define spl_device_create(cls, parent, devt, device, fmt, args...)            \
	class_simple_device_add(cls, devt, device, fmt, ## args)
#define spl_device_destroy(cls, cls_dev, devt)                                \
	class_simple_device_remove(devt)

#endif /* HAVE_CLASS_DEVICE_CREATE */
#endif /* HAVE_DEVICE_CREATE */

#endif /* _SPL_DEVICE_H */
