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

#ifndef _SPL_SUNLDI_H
#define _SPL_SUNLDI_H

#include <sys/types.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/bio.h>
#include <linux/blkdev.h>

#define SECTOR_SIZE 512

typedef struct modlinkage {
	int ml_rev;
	struct modlfs *ml_modlfs;
	struct modldrv *ml_modldrv;
	major_t ml_major;
	unsigned ml_minors;
	void *pad1;
} modlinkage_t;

typedef struct ldi_ident {
	char li_modname[MAXNAMELEN];
	dev_t li_dev;
} *ldi_ident_t;

typedef struct block_device *ldi_handle_t;

extern int ldi_ident_from_mod(struct modlinkage *modlp, ldi_ident_t *lip);
extern void ldi_ident_release(ldi_ident_t li);

#endif /* SPL_SUNLDI_H */
