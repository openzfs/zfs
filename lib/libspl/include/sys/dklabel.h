/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright 1990-2002 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_DKLABEL_H
#define	_SYS_DKLABEL_H



#include <sys/isa_defs.h>
#include <sys/types32.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Miscellaneous defines
 */
#define	DKL_MAGIC	0xDABE		/* magic number */
#define	FKL_MAGIC	0xff		/* magic number for DOS floppies */

#if defined(_SUNOS_VTOC_16)
#define	NDKMAP		16		/* # of logical partitions */
#define	DK_LABEL_LOC	1		/* location of disk label */
#elif defined(_SUNOS_VTOC_8)
#define	NDKMAP		8		/* # of logical partitions */
#define	DK_LABEL_LOC	0		/* location of disk label */
#else
#error "No VTOC format defined."
#endif

#define	LEN_DKL_ASCII	128		/* length of dkl_asciilabel */
#define	LEN_DKL_VVOL	8		/* length of v_volume */
#define	DK_LABEL_SIZE	512		/* size of disk label */
#define	DK_MAX_BLOCKS	0x7fffffff	/* max # of blocks handled */

/*
 * Reserve two cylinders on SCSI disks.
 * One is for the backup disk label and the other is for the deviceid.
 *
 * IPI disks only reserve one cylinder, but they will go away soon.
 * CDROMs do not reserve any cylinders.
 */
#define	DK_ACYL		2

/*
 * Format of a Sun disk label.
 * Resides in cylinder 0, head 0, sector 0.
 *
 * sizeof (struct dk_label) should be 512 (the current sector size),
 * but should the sector size increase, this structure should remain
 * at the beginning of the sector.
 */

/*
 * partition headers:  section 1
 * Returned in struct dk_allmap by ioctl DKIOC[SG]APART (dkio(7I))
 */
struct dk_map {
	uint64_t	dkl_cylno;	/* starting cylinder */
	uint64_t	dkl_nblk;	/* number of blocks;  if == 0, */
					/* partition is undefined */
};

/*
 * partition headers:  section 1
 * Fixed size for on-disk dk_label
 */
struct dk_map32 {
	daddr32_t	dkl_cylno;	/* starting cylinder */
	daddr32_t	dkl_nblk;	/* number of blocks;  if == 0, */
					/* partition is undefined */
};

/*
 * partition headers:  section 2,
 * brought over from AT&T SVr4 vtoc structure.
 */
struct dk_map2 {
	uint16_t	p_tag;		/* ID tag of partition */
	uint16_t	p_flag;		/* permission flag */
};

struct dkl_partition    {
	uint16_t	p_tag;		/* ID tag of partition */
	uint16_t	p_flag;		/* permission flags */
	daddr32_t	p_start;	/* start sector no of partition */
	int32_t		p_size;		/* # of blocks in partition */
};


/*
 * VTOC inclusions from AT&T SVr4
 * Fixed sized types for on-disk VTOC
 */

struct dk_vtoc {
#if defined(_SUNOS_VTOC_16)
	uint32_t v_bootinfo[3];		/* info for mboot (unsupported) */
	uint32_t v_sanity;		/* to verify vtoc sanity */
	uint32_t v_version;		/* layout version */
	char    v_volume[LEN_DKL_VVOL];	/* volume name */
	uint16_t v_sectorsz;		/* sector size in bytes */
	uint16_t v_nparts;		/* number of partitions */
	uint32_t v_reserved[10];	/* free space */
	struct dkl_partition v_part[NDKMAP];	/* partition headers */
	time32_t timestamp[NDKMAP];	/* partition timestamp (unsupported) */
	char    v_asciilabel[LEN_DKL_ASCII];	/* for compatibility    */
#elif defined(_SUNOS_VTOC_8)
	uint32_t	v_version;		/* layout version */
	char		v_volume[LEN_DKL_VVOL];	/* volume name */
	uint16_t	v_nparts;		/* number of partitions  */
	struct dk_map2	v_part[NDKMAP];		/* partition hdrs, sec 2 */
	uint32_t	v_bootinfo[3];		/* info needed by mboot */
	uint32_t	v_sanity;		/* to verify vtoc sanity */
	uint32_t	v_reserved[10];		/* free space */
	time32_t	v_timestamp[NDKMAP];	/* partition timestamp */
#else
#error "No VTOC format defined."
#endif
};

/*
 * define the amount of disk label padding needed to make
 * the entire structure occupy 512 bytes.
 */
#if defined(_SUNOS_VTOC_16)
#define	LEN_DKL_PAD	(DK_LABEL_SIZE - \
			    ((sizeof (struct dk_vtoc) + \
			    (4 * sizeof (uint32_t)) + \
			    (12 * sizeof (uint16_t)) + \
			    (2 * (sizeof (uint16_t))))))
#elif defined(_SUNOS_VTOC_8)
#define	LEN_DKL_PAD	(DK_LABEL_SIZE \
			    - ((LEN_DKL_ASCII) + \
			    (sizeof (struct dk_vtoc)) + \
			    (sizeof (struct dk_map32)  * NDKMAP) + \
			    (14 * (sizeof (uint16_t))) + \
			    (2 * (sizeof (uint16_t)))))
#else
#error "No VTOC format defined."
#endif


struct dk_label {
#if defined(_SUNOS_VTOC_16)
	struct  dk_vtoc dkl_vtoc;	/* vtoc inclusions from AT&T SVr4 */
	uint32_t	dkl_pcyl;	/* # of physical cylinders */
	uint32_t	dkl_ncyl;	/* # of data cylinders */
	uint16_t	dkl_acyl;	/* # of alternate cylinders */
	uint16_t	dkl_bcyl;	/* cyl offset (for fixed head area) */
	uint32_t	dkl_nhead;	/* # of heads */
	uint32_t	dkl_nsect;	/* # of data sectors per track */
	uint16_t	dkl_intrlv;	/* interleave factor */
	uint16_t	dkl_skew;	/* skew factor */
	uint16_t	dkl_apc;	/* alternates per cyl (SCSI only)   */
	uint16_t	dkl_rpm;	/* revolutions per minute */
	uint16_t	dkl_write_reinstruct;	/* # sectors to skip, writes */
	uint16_t	dkl_read_reinstruct;	/* # sectors to skip, reads  */
	uint16_t	dkl_extra[4];	/* for compatible expansion */
	char		dkl_pad[LEN_DKL_PAD];	/* unused part of 512 bytes */
#elif defined(_SUNOS_VTOC_8)
	char		dkl_asciilabel[LEN_DKL_ASCII]; /* for compatibility */
	struct dk_vtoc	dkl_vtoc;	/* vtoc inclusions from AT&T SVr4 */
	uint16_t	dkl_write_reinstruct;	/* # sectors to skip, writes */
	uint16_t	dkl_read_reinstruct;	/* # sectors to skip, reads */
	char		dkl_pad[LEN_DKL_PAD]; /* unused part of 512 bytes */
	uint16_t	dkl_rpm;	/* rotations per minute */
	uint16_t	dkl_pcyl;	/* # physical cylinders */
	uint16_t	dkl_apc;	/* alternates per cylinder */
	uint16_t	dkl_obs1;	/* obsolete */
	uint16_t	dkl_obs2;	/* obsolete */
	uint16_t	dkl_intrlv;	/* interleave factor */
	uint16_t	dkl_ncyl;	/* # of data cylinders */
	uint16_t	dkl_acyl;	/* # of alternate cylinders */
	uint16_t	dkl_nhead;	/* # of heads in this partition */
	uint16_t	dkl_nsect;	/* # of 512 byte sectors per track */
	uint16_t	dkl_obs3;	/* obsolete */
	uint16_t	dkl_obs4;	/* obsolete */
	struct dk_map32	dkl_map[NDKMAP]; /* logical partition headers */
#else
#error "No VTOC format defined."
#endif
	uint16_t	dkl_magic;	/* identifies this label format */
	uint16_t	dkl_cksum;	/* xor checksum of sector */
};

#if defined(_SUNOS_VTOC_16)
#define	dkl_asciilabel	dkl_vtoc.v_asciilabel
#define	v_timestamp	timestamp

#elif defined(_SUNOS_VTOC_8)

/*
 * These defines are for historic compatibility with old drivers.
 */
#define	dkl_gap1	dkl_obs1	/* used to be gap1 */
#define	dkl_gap2	dkl_obs2	/* used to be gap2 */
#define	dkl_bhead	dkl_obs3	/* used to be label head offset */
#define	dkl_ppart	dkl_obs4	/* used to by physical partition */
#else
#error "No VTOC format defined."
#endif

struct fk_label {			/* DOS floppy label */
	uchar_t  fkl_type;
	uchar_t  fkl_magich;
	uchar_t  fkl_magicl;
	uchar_t  filler;
};

/*
 * Layout of stored fabricated device id  (on-disk)
 */
#define	DK_DEVID_BLKSIZE	(512)
#define	DK_DEVID_SIZE		(DK_DEVID_BLKSIZE - ((sizeof (uchar_t) * 7)))
#define	DK_DEVID_REV_MSB	(0)
#define	DK_DEVID_REV_LSB	(1)

struct dk_devid {
	uchar_t	dkd_rev_hi;			/* revision (MSB) */
	uchar_t	dkd_rev_lo;			/* revision (LSB) */
	uchar_t	dkd_flags;			/* flags (not used yet) */
	uchar_t	dkd_devid[DK_DEVID_SIZE];	/* devid stored here */
	uchar_t	dkd_checksum3;			/* checksum (MSB) */
	uchar_t	dkd_checksum2;
	uchar_t	dkd_checksum1;
	uchar_t	dkd_checksum0;			/* checksum (LSB) */
};

#define	DKD_GETCHKSUM(dkd)	((dkd)->dkd_checksum3 << 24) + \
				((dkd)->dkd_checksum2 << 16) + \
				((dkd)->dkd_checksum1 << 8)  + \
				((dkd)->dkd_checksum0)

#define	DKD_FORMCHKSUM(c, dkd)	(dkd)->dkd_checksum3 = hibyte(hiword((c))); \
				(dkd)->dkd_checksum2 = lobyte(hiword((c))); \
				(dkd)->dkd_checksum1 = hibyte(loword((c))); \
				(dkd)->dkd_checksum0 = lobyte(loword((c)));
#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DKLABEL_H */
