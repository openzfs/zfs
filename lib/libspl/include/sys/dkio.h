// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_DKIO_H
#define	_SYS_DKIO_H



#include <sys/dklabel.h>	/* Needed for NDKMAP define */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Structures and definitions for disk io control commands
 */

/*
 * Structures used as data by ioctl calls.
 */

#define	DK_DEVLEN	16		/* device name max length, including */
					/* unit # & NULL (ie - "xyc1") */

/*
 * Used for controller info
 */
struct dk_cinfo {
	char	dki_cname[DK_DEVLEN];	/* controller name (no unit #) */
	ushort_t dki_ctype;		/* controller type */
	ushort_t dki_flags;		/* flags */
	ushort_t dki_cnum;		/* controller number */
	uint_t	dki_addr;		/* controller address */
	uint_t	dki_space;		/* controller bus type */
	uint_t	dki_prio;		/* interrupt priority */
	uint_t	dki_vec;		/* interrupt vector */
	char	dki_dname[DK_DEVLEN];	/* drive name (no unit #) */
	uint_t	dki_unit;		/* unit number */
	ushort_t dki_partition;		/* partition number */
	ushort_t dki_maxtransfer;	/* max. transfer size in DEV_BSIZE */
};

/*
 * Controller types
 */
#define	DKC_UNKNOWN	0
#define	DKC_CDROM	1	/* CD-ROM, SCSI or otherwise */
#define	DKC_WDC2880	2
#define	DKC_XXX_0	3	/* unassigned */
#define	DKC_XXX_1	4	/* unassigned */
#define	DKC_DSD5215	5
#define	DKC_ACB4000	7
#define	DKC_MD21	8
#define	DKC_XXX_2	9	/* unassigned */
#define	DKC_NCRFLOPPY	10
#define	DKC_SMSFLOPPY	12
#define	DKC_SCSI_CCS	13	/* SCSI CCS compatible */
#define	DKC_INTEL82072	14	/* native floppy chip */
#define	DKC_MD		16	/* meta-disk (virtual-disk) driver */
#define	DKC_INTEL82077	19	/* 82077 floppy disk controller */
#define	DKC_DIRECT	20	/* Intel direct attached device i.e. IDE */
#define	DKC_PCMCIA_MEM	21	/* PCMCIA memory disk-like type */
#define	DKC_PCMCIA_ATA	22	/* PCMCIA AT Attached type */
#define	DKC_VBD		23	/* virtual block device */

/*
 * Sun reserves up through 1023
 */

#define	DKC_CUSTOMER_BASE	1024

/*
 * Flags
 */
#define	DKI_BAD144	0x01	/* use DEC std 144 bad sector fwding */
#define	DKI_MAPTRK	0x02	/* controller does track mapping */
#define	DKI_FMTTRK	0x04	/* formats only full track at a time */
#define	DKI_FMTVOL	0x08	/* formats only full volume at a time */
#define	DKI_FMTCYL	0x10	/* formats only full cylinders at a time */
#define	DKI_HEXUNIT	0x20	/* unit number is printed as 3 hex digits */
#define	DKI_PCMCIA_PFD	0x40	/* PCMCIA pseudo-floppy memory card */

/*
 * Used for all partitions
 */
struct dk_allmap {
	struct dk_map	dka_map[NDKMAP];
};

#if defined(_SYSCALL32)
struct dk_allmap32 {
	struct dk_map32	dka_map[NDKMAP];
};
#endif /* _SYSCALL32 */

/*
 * Definition of a disk's geometry
 */
struct dk_geom {
	unsigned short	dkg_ncyl;	/* # of data cylinders */
	unsigned short	dkg_acyl;	/* # of alternate cylinders */
	unsigned short	dkg_bcyl;	/* cyl offset (for fixed head area) */
	unsigned short	dkg_nhead;	/* # of heads */
	unsigned short	dkg_obs1;	/* obsolete */
	unsigned short	dkg_nsect;	/* # of data sectors per track */
	unsigned short	dkg_intrlv;	/* interleave factor */
	unsigned short	dkg_obs2;	/* obsolete */
	unsigned short	dkg_obs3;	/* obsolete */
	unsigned short	dkg_apc;	/* alternates per cyl (SCSI only) */
	unsigned short	dkg_rpm;	/* revolutions per minute */
	unsigned short	dkg_pcyl;	/* # of physical cylinders */
	unsigned short	dkg_write_reinstruct;	/* # sectors to skip, writes */
	unsigned short	dkg_read_reinstruct;	/* # sectors to skip, reads */
	unsigned short	dkg_extra[7];	/* for compatible expansion */
};

/*
 * These defines are for historic compatibility with old drivers.
 */
#define	dkg_bhead	dkg_obs1	/* used to be head offset */
#define	dkg_gap1	dkg_obs2	/* used to be gap1 */
#define	dkg_gap2	dkg_obs3	/* used to be gap2 */

/*
 * Disk io control commands
 * Warning: some other ioctls with the DIOC prefix exist elsewhere.
 * The Generic DKIOC numbers are from	0   -  50.
 *	The Floppy Driver uses		51  - 100.
 *	The Hard Disk (except SCSI)	101 - 106.	(these are obsolete)
 *	The CDROM Driver		151 - 200.
 *	The USCSI ioctl			201 - 250.
 */
#define	DKIOC		(0x04 << 8)

/*
 * The following ioctls are generic in nature and need to be
 * supported as appropriate by all disk drivers
 */
#define	DKIOCGGEOM	(DKIOC|1)		/* Get geometry */
#define	DKIOCINFO	(DKIOC|3)		/* Get info */
#define	DKIOCEJECT	(DKIOC|6)		/* Generic 'eject' */
#define	DKIOCGVTOC	(DKIOC|11)		/* Get VTOC */
#define	DKIOCSVTOC	(DKIOC|12)		/* Set VTOC & Write to Disk */

/*
 * Disk Cache Controls.  These ioctls should be supported by
 * all disk drivers.
 *
 * DKIOCFLUSHWRITECACHE when used from user-mode ignores the ioctl
 * argument, but it should be passed as NULL to allow for future
 * reinterpretation.  From user-mode, this ioctl request is synchronous.
 *
 * When invoked from within the kernel, the arg can be NULL to indicate
 * a synchronous request or can be the address of a struct dk_callback
 * to request an asynchronous callback when the flush request is complete.
 * In this case, the flag to the ioctl must include FKIOCTL and the
 * dkc_callback field of the pointed to struct must be non-null or the
 * request is made synchronously.
 *
 * In the callback case: if the ioctl returns 0, a callback WILL be performed.
 * If the ioctl returns non-zero, a callback will NOT be performed.
 * NOTE: In some cases, the callback may be done BEFORE the ioctl call
 * returns.  The caller's locking strategy should be prepared for this case.
 */
#define	DKIOCFLUSHWRITECACHE	(DKIOC|34)	/* flush cache to phys medium */

struct dk_callback {
	void (*dkc_callback)(void *dkc_cookie, int error);
	void *dkc_cookie;
	int dkc_flag;
};

/* bit flag definitions for dkc_flag */
#define	FLUSH_VOLATILE		0x1	/* Bit 0: if set, only flush */
					/* volatile cache; otherwise, flush */
					/* volatile and non-volatile cache */

#define	DKIOCGETWCE		(DKIOC|36)	/* Get current write cache */
						/* enablement status */
#define	DKIOCSETWCE		(DKIOC|37)	/* Enable/Disable write cache */

/*
 * The following ioctls are used by Sun drivers to communicate
 * with their associated format routines. Support of these ioctls
 * is not required of foreign drivers
 */
#define	DKIOCSGEOM	(DKIOC|2)		/* Set geometry */
#define	DKIOCSAPART	(DKIOC|4)		/* Set all partitions */
#define	DKIOCGAPART	(DKIOC|5)		/* Get all partitions */
#define	DKIOCG_PHYGEOM	(DKIOC|32)		/* get physical geometry */
#define	DKIOCG_VIRTGEOM	(DKIOC|33)		/* get virtual geometry */

/*
 * The following ioctl's are removable media support
 */
#define	DKIOCLOCK	(DKIOC|7)	/* Generic 'lock' */
#define	DKIOCUNLOCK	(DKIOC|8)	/* Generic 'unlock' */
#define	DKIOCSTATE	(DKIOC|13)	/* Inquire insert/eject state */
#define	DKIOCREMOVABLE	(DKIOC|16)	/* is media removable */


/*
 * ioctl for hotpluggable devices
 */
#define	DKIOCHOTPLUGGABLE	(DKIOC|35)	/* is hotpluggable */

/*
 * Ioctl to force driver to re-read the alternate partition and rebuild
 * the internal defect map.
 */
#define	DKIOCADDBAD	(DKIOC|20)	/* Re-read the alternate map (IDE) */
#define	DKIOCGETDEF	(DKIOC|21)	/* read defect list (IDE)	   */

/*
 * Used by applications to get disk defect information from IDE
 * drives.
 */
#ifdef _SYSCALL32
struct defect_header32 {
	int		head;
	caddr32_t	buffer;
};
#endif /* _SYSCALL32 */

struct defect_header {
	int		head;
	caddr_t		buffer;
};

#define	DKIOCPARTINFO	(DKIOC|22)	/* Get partition or slice parameters */

/*
 * Used by applications to get partition or slice information
 */
#ifdef _SYSCALL32
struct part_info32 {
	uint32_t	p_start;
	int		p_length;
};
#endif /* _SYSCALL32 */

struct part_info {
	uint64_t	p_start;
	int		p_length;
};

/* The following ioctls are for Optical Memory Device */
#define	DKIOC_EBP_ENABLE  (DKIOC|40)	/* enable by pass erase on write */
#define	DKIOC_EBP_DISABLE (DKIOC|41)	/* disable by pass erase on write */

/*
 * This state enum is the argument passed to the DKIOCSTATE ioctl.
 */
enum dkio_state { DKIO_NONE, DKIO_EJECTED, DKIO_INSERTED, DKIO_DEV_GONE };

#define	DKIOCGMEDIAINFO	(DKIOC|42)	/* get information about the media */

/*
 * ioctls to read/write mboot info.
 */
#define	DKIOCGMBOOT	(DKIOC|43)	/* get mboot info */
#define	DKIOCSMBOOT	(DKIOC|44)	/* set mboot info */

/*
 * ioctl to get the device temperature.
 */
#define	DKIOCGTEMPERATURE	(DKIOC|45)	/* get temperature */

/*
 * Used for providing the temperature.
 */

struct	dk_temperature	{
	uint_t		dkt_flags;	/* Flags */
	short		dkt_cur_temp;	/* Current disk temperature */
	short		dkt_ref_temp;	/* reference disk temperature */
};

#define	DKT_BYPASS_PM		0x1
#define	DKT_INVALID_TEMP	0xFFFF


/*
 * Used for Media info or the current profile info
 */
struct dk_minfo {
	uint_t		dki_media_type;	/* Media type or profile info */
	uint_t		dki_lbsize;	/* Logical blocksize of media */
	diskaddr_t	dki_capacity;	/* Capacity as # of dki_lbsize blks */
};

/*
 * Media types or profiles known
 */
#define	DK_UNKNOWN		0x00	/* Media inserted - type unknown */


/*
 * SFF 8090 Specification Version 3, media types 0x01 - 0xfffe are retained to
 * maintain compatibility with SFF8090.  The following define the
 * optical media type.
 */
#define	DK_REMOVABLE_DISK	0x02 /* Removable Disk */
#define	DK_MO_ERASABLE		0x03 /* MO Erasable */
#define	DK_MO_WRITEONCE		0x04 /* MO Write once */
#define	DK_AS_MO		0x05 /* AS MO */
#define	DK_CDROM		0x08 /* CDROM */
#define	DK_CDR			0x09 /* CD-R */
#define	DK_CDRW			0x0A /* CD-RW */
#define	DK_DVDROM		0x10 /* DVD-ROM */
#define	DK_DVDR			0x11 /* DVD-R */
#define	DK_DVDRAM		0x12 /* DVD_RAM or DVD-RW */

/*
 * Media types for other rewritable magnetic media
 */
#define	DK_FIXED_DISK		0x10001	/* Fixed disk SCSI or otherwise */
#define	DK_FLOPPY		0x10002 /* Floppy media */
#define	DK_ZIP			0x10003 /* IOMEGA ZIP media */
#define	DK_JAZ			0x10004 /* IOMEGA JAZ media */

#define	DKIOCSETEFI	(DKIOC|17)		/* Set EFI info */
#define	DKIOCGETEFI	(DKIOC|18)		/* Get EFI info */

#define	DKIOCPARTITION	(DKIOC|9)		/* Get partition info */

/*
 * Ioctls to get/set volume capabilities related to Logical Volume Managers.
 * They include the ability to get/set capabilities and to issue a read to a
 * specific underlying device of a replicated device.
 */

#define	DKIOCGETVOLCAP	(DKIOC | 25)	/* Get volume capabilities */
#define	DKIOCSETVOLCAP	(DKIOC | 26)	/* Set volume capabilities */
#define	DKIOCDMR	(DKIOC | 27)	/* Issue a directed read */

typedef uint_t volcapinfo_t;

typedef uint_t volcapset_t;

#define	DKV_ABR_CAP 0x00000001		/* Support Appl.Based Recovery */
#define	DKV_DMR_CAP 0x00000002		/* Support Directed  Mirror Read */

typedef struct volcap {
	volcapinfo_t vc_info;	/* Capabilities available */
	volcapset_t vc_set;	/* Capabilities set */
} volcap_t;

#define	VOL_SIDENAME 256

typedef struct vol_directed_rd {
	int		vdr_flags;
	offset_t	vdr_offset;
	size_t		vdr_nbytes;
	size_t		vdr_bytesread;
	void		*vdr_data;
	int		vdr_side;
	char		vdr_side_name[VOL_SIDENAME];
} vol_directed_rd_t;

#define	DKV_SIDE_INIT		(-1)
#define	DKV_DMR_NEXT_SIDE	0x00000001
#define	DKV_DMR_DONE		0x00000002
#define	DKV_DMR_ERROR		0x00000004
#define	DKV_DMR_SUCCESS		0x00000008
#define	DKV_DMR_SHORT		0x00000010

#ifdef _MULTI_DATAMODEL
#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack(4)
#endif
typedef struct vol_directed_rd32 {
	int32_t		vdr_flags;
	offset_t	vdr_offset;	/* 64-bit element on 32-bit alignment */
	size32_t	vdr_nbytes;
	size32_t	vdr_bytesread;
	caddr32_t	vdr_data;
	int32_t		vdr_side;
	char		vdr_side_name[VOL_SIDENAME];
} vol_directed_rd32_t;
#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack()
#endif
#endif	/* _MULTI_DATAMODEL */

/*
 * The ioctl is used to fetch disk's device type, vendor ID,
 * model number/product ID, firmware revision and serial number together.
 *
 * Currently there are two device types - DKD_ATA_TYPE which means the
 * disk is driven by cmdk/ata or dad/uata driver, and DKD_SCSI_TYPE
 * which means the disk is driven by sd/scsi hba driver.
 */
#define	DKIOC_GETDISKID	(DKIOC|46)

/* These two labels are for dkd_dtype of dk_disk_id_t */
#define	DKD_ATA_TYPE	0x01 /* ATA disk or legacy mode SATA disk */
#define	DKD_SCSI_TYPE	0x02 /* SCSI disk or native mode SATA disk */

#define	DKD_ATA_MODEL	40	/* model number length */
#define	DKD_ATA_FWVER	8	/* firmware revision length */
#define	DKD_ATA_SERIAL	20	/* serial number length */

#define	DKD_SCSI_VENDOR	8	/* vendor ID length */
#define	DKD_SCSI_PRODUCT 16	/* product ID length */
#define	DKD_SCSI_REVLEVEL 4	/* revision level length */
#define	DKD_SCSI_SERIAL 12	/* serial number length */

/*
 * The argument type for DKIOC_GETDISKID ioctl.
 */
typedef struct dk_disk_id {
	uint_t	dkd_dtype;
	union {
		struct {
			char dkd_amodel[DKD_ATA_MODEL];		/* 40 bytes */
			char dkd_afwver[DKD_ATA_FWVER];		/* 8 bytes */
			char dkd_aserial[DKD_ATA_SERIAL];	/* 20 bytes */
		} ata_disk_id;
		struct {
			char dkd_svendor[DKD_SCSI_VENDOR];	/* 8 bytes */
			char dkd_sproduct[DKD_SCSI_PRODUCT];	/* 16 bytes */
			char dkd_sfwver[DKD_SCSI_REVLEVEL];	/* 4 bytes */
			char dkd_sserial[DKD_SCSI_SERIAL];	/* 12 bytes */
		} scsi_disk_id;
	} disk_id;
} dk_disk_id_t;

/*
 * The ioctl is used to update the firmware of device.
 */
#define	DKIOC_UPDATEFW		(DKIOC|47)

/* The argument type for DKIOC_UPDATEFW ioctl */
typedef struct dk_updatefw {
	caddr_t		dku_ptrbuf;	/* pointer to firmware buf */
	uint_t		dku_size;	/* firmware buf length */
	uint8_t		dku_type;	/* firmware update type */
} dk_updatefw_t;

#ifdef _SYSCALL32
typedef struct dk_updatefw_32 {
	caddr32_t	dku_ptrbuf;	/* pointer to firmware buf */
	uint_t		dku_size;	/* firmware buf length */
	uint8_t		dku_type;	/* firmware update type */
} dk_updatefw_32_t;
#endif /* _SYSCALL32 */

/*
 * firmware update type - temporary or permanent use
 */
#define	FW_TYPE_TEMP	0x0		/* temporary use */
#define	FW_TYPE_PERM	0x1		/* permanent use */


#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DKIO_H */
