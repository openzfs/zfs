/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */


/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


#ifndef _SYS_VTOC_H
#define	_SYS_VTOC_H

#include <sys/dklabel.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 *	Note:  the VTOC is not implemented fully, nor in the manner
 *	that AT&T implements it.  AT&T puts the vtoc structure
 *	into a sector, usually the second sector (pdsector is first).
 *
 *	Sun incorporates the tag, flag, version, and volume vtoc fields into
 *	its Disk Label, which already has some vtoc-equivalent fields.
 *	Upon reading the vtoc with read_vtoc(), the following exceptions
 *	occur:
 *		v_bootinfo [all]	returned as zero
 *		v_sanity		returned as VTOC_SANE
 *						if Disk Label was sane
 *		v_sectorsz		returned as 512
 *		v_reserved [all]	returned as zero
 *		timestamp [all]		returned as zero
 *
 *	See  dklabel.h, read_vtoc(), and write_vtoc().
 */

#define	V_NUMPAR 	NDKMAP		/* The number of partitions */
					/* (from dkio.h) */

#define	VTOC_SANE	0x600DDEEE	/* Indicates a sane VTOC */
#define	V_VERSION	0x01		/* layout version number */
#define	V_EXTVERSION	V_VERSION	/* extvtoc layout version number */

/*
 * Partition identification tags
 */
#define	V_UNASSIGNED	0x00		/* unassigned partition */
#define	V_BOOT		0x01		/* Boot partition */
#define	V_ROOT		0x02		/* Root filesystem */
#define	V_SWAP		0x03		/* Swap filesystem */
#define	V_USR		0x04		/* Usr filesystem */
#define	V_BACKUP	0x05		/* full disk */
#define	V_STAND		0x06		/* Stand partition */
#define	V_VAR		0x07		/* Var partition */
#define	V_HOME		0x08		/* Home partition */
#define	V_ALTSCTR	0x09		/* Alternate sector partition */
#define	V_CACHE		0x0a		/* Cache (cachefs) partition */
#define	V_RESERVED	0x0b		/* SMI reserved data */

/*
 * Partition permission flags
 */
#define	V_UNMNT		0x01		/* Unmountable partition */
#define	V_RONLY		0x10		/* Read only */

/*
 * error codes for reading & writing vtoc
 */
#define	VT_ERROR	(-2)		/* errno supplies specific error */
#define	VT_EIO		(-3)		/* I/O error accessing vtoc */
#define	VT_EINVAL	(-4)		/* illegal value in vtoc or request */
#define	VT_ENOTSUP	(-5)		/* VTOC op. not supported */
#define	VT_ENOSPC	(-6)		/* requested space not found */
#define	VT_EOVERFLOW	(-7)		/* VTOC op. data struct limited */

struct partition	{
	ushort_t 	p_tag;		/* ID tag of partition */
	ushort_t 	p_flag;		/* permission flags */
	uint64_t	p_start;	/* start sector no of partition */
	long		p_size;		/* # of blocks in partition */
};

struct vtoc {
	unsigned long	v_bootinfo[3];	/* info needed by mboot (unsupported) */
	unsigned long	v_sanity;	/* to verify vtoc sanity */
	unsigned long	v_version;	/* layout version */
	char	v_volume[LEN_DKL_VVOL];	/* volume name */
	ushort_t	v_sectorsz;	/* sector size in bytes */
	ushort_t	v_nparts;	/* number of partitions */
	unsigned long	v_reserved[10];	/* free space */
	struct partition v_part[V_NUMPAR]; /* partition headers */
	time_t	timestamp[V_NUMPAR];	/* partition timestamp (unsupported) */
	char	v_asciilabel[LEN_DKL_ASCII];	/* for compatibility */
};

struct extpartition {
	ushort_t p_tag;			/* ID tag of partition */
	ushort_t p_flag;		/* permission flags */
	ushort_t p_pad[2];
	diskaddr_t p_start;		/* start sector no of partition */
	diskaddr_t p_size;		/* # of blocks in partition */
};


struct extvtoc {
	uint64_t	v_bootinfo[3];	/* info needed by mboot (unsupported) */
	uint64_t	v_sanity;	/* to verify vtoc sanity */
	uint64_t	v_version;	/* layout version */
	char	v_volume[LEN_DKL_VVOL];	/* volume name */
	ushort_t	v_sectorsz;	/* sector size in bytes */
	ushort_t	v_nparts;	/* number of partitions */
	ushort_t	pad[2];
	uint64_t	v_reserved[10];
	struct extpartition v_part[V_NUMPAR]; /* partition headers */
	uint64_t timestamp[V_NUMPAR];	/* partition timestamp (unsupported) */
	char	v_asciilabel[LEN_DKL_ASCII];	/* for compatibility */
};

#ifdef _KERNEL
#define	extvtoctovtoc(extv, v)						\
	{								\
	int i;								\
	v.v_bootinfo[0]		= (unsigned long)extv.v_bootinfo[0];	\
	v.v_bootinfo[1]		= (unsigned long)extv.v_bootinfo[1];	\
	v.v_bootinfo[2]		= (unsigned long)extv.v_bootinfo[2];	\
	v.v_sanity		= (unsigned long)extv.v_sanity;		\
	v.v_version		= (unsigned long)extv.v_version;	\
	bcopy(extv.v_volume, v.v_volume, LEN_DKL_VVOL);			\
	v.v_sectorsz		= extv.v_sectorsz;			\
	v.v_nparts		= extv.v_nparts;			\
	for (i = 0; i < 10; i++)					\
		v.v_reserved[i] = (unsigned long)extv.v_reserved[i];	\
	for (i = 0; i < V_NUMPAR; i++) {				\
		v.v_part[i].p_tag = extv.v_part[i].p_tag;		\
		v.v_part[i].p_flag = extv.v_part[i].p_flag;		\
		v.v_part[i].p_start = (uint64_t)extv.v_part[i].p_start;	\
		v.v_part[i].p_size = (long)extv.v_part[i].p_size;	\
		v.timestamp[i] = (time_t)extv.timestamp[i];		\
	}								\
	bcopy(extv.v_asciilabel, v.v_asciilabel, LEN_DKL_ASCII);	\
	}

#define	vtoctoextvtoc(v, extv)						\
	{								\
	int i;								\
	extv.v_bootinfo[0]	= (uint64_t)v.v_bootinfo[0];		\
	extv.v_bootinfo[1]	= (uint64_t)v.v_bootinfo[1];		\
	extv.v_bootinfo[2]	= (uint64_t)v.v_bootinfo[2];		\
	extv.v_sanity		= (uint64_t)v.v_sanity;			\
	extv.v_version		= (uint64_t)v.v_version;		\
	bcopy(v.v_volume, extv.v_volume, LEN_DKL_VVOL);			\
	extv.v_sectorsz		= v.v_sectorsz;				\
	extv.v_nparts		= v.v_nparts;				\
	for (i = 0; i < 10; i++)					\
		extv.v_reserved[i] = (uint64_t)v.v_reserved[i];		\
	for (i = 0; i < V_NUMPAR; i++) {				\
		extv.v_part[i].p_tag = v.v_part[i].p_tag;		\
		extv.v_part[i].p_flag = v.v_part[i].p_flag;		\
		extv.v_part[i].p_start =				\
		    (diskaddr_t)(unsigned long)v.v_part[i].p_start;	\
		extv.v_part[i].p_size =					\
		    (diskaddr_t)(unsigned long)v.v_part[i].p_size;	\
		extv.timestamp[i] = (uint64_t)v.timestamp[i];		\
	}								\
	bcopy(v.v_asciilabel, extv.v_asciilabel, LEN_DKL_ASCII);	\
	}
#endif /* _KERNEL */

#if defined(_SYSCALL32)
struct partition32	{
	uint16_t	p_tag;		/* ID tag of partition */
	uint16_t	p_flag;		/* permission flags */
	daddr32_t	p_start;	/* start sector no of partition */
	int32_t		p_size;		/* # of blocks in partition */
};

struct vtoc32 {
	uint32_t	v_bootinfo[3];	/* info needed by mboot (unsupported) */
	uint32_t	v_sanity;	/* to verify vtoc sanity */
	uint32_t	v_version;	/* layout version */
	char	v_volume[LEN_DKL_VVOL];	/* volume name */
	uint16_t	v_sectorsz;	/* sector size in bytes */
	uint16_t	v_nparts;	/* number of partitions */
	uint32_t	v_reserved[10];	/* free space */
	struct partition32 v_part[V_NUMPAR]; /* partition headers */
	time32_t timestamp[V_NUMPAR];	/* partition timestamp (unsupported) */
	char	v_asciilabel[LEN_DKL_ASCII];	/* for compatibility */
};

#define	vtoc32tovtoc(v32, v)				\
	{						\
	int i;						\
	v.v_bootinfo[0]		= v32.v_bootinfo[0];	\
	v.v_bootinfo[1]		= v32.v_bootinfo[1];	\
	v.v_bootinfo[2]		= v32.v_bootinfo[2];	\
	v.v_sanity		= v32.v_sanity;		\
	v.v_version		= v32.v_version;	\
	bcopy(v32.v_volume, v.v_volume, LEN_DKL_VVOL);	\
	v.v_sectorsz		= v32.v_sectorsz;	\
	v.v_nparts		= v32.v_nparts;		\
	v.v_version		= v32.v_version;	\
	for (i = 0; i < 10; i++)			\
		v.v_reserved[i] = v32.v_reserved[i];	\
	for (i = 0; i < V_NUMPAR; i++) {		\
		v.v_part[i].p_tag = (ushort_t)v32.v_part[i].p_tag;	\
		v.v_part[i].p_flag = (ushort_t)v32.v_part[i].p_flag;	\
		v.v_part[i].p_start = (unsigned)v32.v_part[i].p_start;	\
		v.v_part[i].p_size = (unsigned)v32.v_part[i].p_size;	\
	}						\
	for (i = 0; i < V_NUMPAR; i++)			\
		v.timestamp[i] = (time_t)v32.timestamp[i];		\
	bcopy(v32.v_asciilabel, v.v_asciilabel, LEN_DKL_ASCII);		\
	}

#define	vtoc32toextvtoc(v32, extv)					\
	{								\
	int i;								\
	extv.v_bootinfo[0]		= v32.v_bootinfo[0];		\
	extv.v_bootinfo[1]		= v32.v_bootinfo[1];		\
	extv.v_bootinfo[2]		= v32.v_bootinfo[2];		\
	extv.v_sanity		= v32.v_sanity;				\
	extv.v_version		= v32.v_version;			\
	bcopy(v32.v_volume, extv.v_volume, LEN_DKL_VVOL);		\
	extv.v_sectorsz		= v32.v_sectorsz;			\
	extv.v_nparts		= v32.v_nparts;				\
	extv.v_version		= v32.v_version;			\
	for (i = 0; i < 10; i++)					\
		extv.v_reserved[i] = v32.v_reserved[i];			\
	for (i = 0; i < V_NUMPAR; i++) {				\
		extv.v_part[i].p_tag = (ushort_t)v32.v_part[i].p_tag;	\
		extv.v_part[i].p_flag = (ushort_t)v32.v_part[i].p_flag;	\
		extv.v_part[i].p_start = (diskaddr_t)v32.v_part[i].p_start; \
		extv.v_part[i].p_size = (diskaddr_t)v32.v_part[i].p_size; \
		extv.timestamp[i] = (time_t)v32.timestamp[i];		\
	}								\
	bcopy(v32.v_asciilabel, extv.v_asciilabel, LEN_DKL_ASCII);	\
	}


#define	vtoctovtoc32(v, v32)				\
	{						\
	int i;						\
	v32.v_bootinfo[0]	= v.v_bootinfo[0];	\
	v32.v_bootinfo[1]	= v.v_bootinfo[1];	\
	v32.v_bootinfo[2]	= v.v_bootinfo[2];	\
	v32.v_sanity		= v.v_sanity;		\
	v32.v_version		= v.v_version;		\
	bcopy(v.v_volume, v32.v_volume, LEN_DKL_VVOL);	\
	v32.v_sectorsz		= v.v_sectorsz;		\
	v32.v_nparts		= v.v_nparts;		\
	v32.v_version		= v.v_version;		\
	for (i = 0; i < 10; i++)			\
		v32.v_reserved[i] = v.v_reserved[i];	\
	for (i = 0; i < V_NUMPAR; i++) {		\
		v32.v_part[i].p_tag = (ushort_t)v.v_part[i].p_tag;	\
		v32.v_part[i].p_flag = (ushort_t)v.v_part[i].p_flag;	\
		v32.v_part[i].p_start = (unsigned)v.v_part[i].p_start;	\
		v32.v_part[i].p_size = (unsigned)v.v_part[i].p_size;	\
	}						\
	for (i = 0; i < V_NUMPAR; i++) {		\
		if (v.timestamp[i] > TIME32_MAX)	\
			v32.timestamp[i] = TIME32_MAX;	\
		else 					\
			v32.timestamp[i] = (time32_t)v.timestamp[i];	\
	}						\
	bcopy(v.v_asciilabel, v32.v_asciilabel, LEN_DKL_ASCII);		\
	}

#define	extvtoctovtoc32(extv, v32)				\
	{						\
	int i;						\
	v32.v_bootinfo[0]	= extv.v_bootinfo[0];	\
	v32.v_bootinfo[1]	= extv.v_bootinfo[1];	\
	v32.v_bootinfo[2]	= extv.v_bootinfo[2];	\
	v32.v_sanity		= extv.v_sanity;		\
	v32.v_version		= extv.v_version;		\
	bcopy(extv.v_volume, v32.v_volume, LEN_DKL_VVOL);	\
	v32.v_sectorsz		= extv.v_sectorsz;		\
	v32.v_nparts		= extv.v_nparts;		\
	v32.v_version		= extv.v_version;		\
	for (i = 0; i < 10; i++)			\
		v32.v_reserved[i] = extv.v_reserved[i];	\
	for (i = 0; i < V_NUMPAR; i++) {		\
		v32.v_part[i].p_tag = (ushort_t)extv.v_part[i].p_tag;	\
		v32.v_part[i].p_flag = (ushort_t)extv.v_part[i].p_flag;	\
		v32.v_part[i].p_start = (unsigned)extv.v_part[i].p_start; \
		v32.v_part[i].p_size = (unsigned)extv.v_part[i].p_size;	\
	}						\
	for (i = 0; i < V_NUMPAR; i++) {		\
		if (extv.timestamp[i] > TIME32_MAX)	\
			v32.timestamp[i] = TIME32_MAX;	\
		else 					\
			v32.timestamp[i] = (time32_t)extv.timestamp[i];	\
	}						\
	bcopy(extv.v_asciilabel, v32.v_asciilabel, LEN_DKL_ASCII);	\
	}


#endif /* _SYSCALL32 */

/*
 * These defines are the mode parameter for the checksum routines.
 */
#define	CK_CHECKSUM	0	/* check checksum */
#define	CK_MAKESUM	1	/* generate checksum */

#if defined(__STDC__)

extern	int	read_vtoc(int, struct vtoc *);
extern	int	write_vtoc(int, struct vtoc *);
extern	int	read_extvtoc(int, struct extvtoc *);
extern	int	write_extvtoc(int, struct extvtoc *);

#else

extern	int	read_vtoc();
extern	int	write_vtoc();
extern	int	read_extvtoc();
extern	int	write_extvtoc();

#endif 	/* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VTOC_H */
