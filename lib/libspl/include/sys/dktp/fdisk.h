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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*	Copyright (c) 1984, 1986, 1987, 1988 AT&T	*/
/*	  All Rights Reserved  	*/


#ifndef _SYS_DKTP_FDISK_H
#define	_SYS_DKTP_FDISK_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * fdisk.h
 * This file defines the structure of physical disk sector 0 for use on
 * AT386 systems.  The format of this sector is constrained by the ROM
 * BIOS and MS-DOS conventions.
 * Note that this block does not define the partitions used by the unix
 * driver.  The unix partitions are obtained from the VTOC.
 */

/*
 * the MAX values are the maximum usable values for BIOS chs values
 * The MAX_CYL value of 1022 is the maximum usable value
 *   the value of 1023 is a fence value,
 *   indicating no CHS geometry exists for the corresponding LBA value.
 * HEAD range [ 0 .. MAX_HEAD ], so number of heads is (MAX_HEAD + 1)
 * SECT range [ 1 .. MAX_SECT ], so number of sectors is (MAX_SECT)
 */
#define	MAX_SECT	(63)
#define	MAX_CYL		(1022)
#define	MAX_HEAD	(254)

/*
 * BOOTSZ was reduced from 446 to 440 bytes to NOT overwrite the Windows
 * Vista DISKID. Otherwise Vista won't boot from Solaris GRUB in a dual-boot
 * setup.
 * The actual size of mboot code is 425 bytes while that of GRUB stage1 is
 * 423 bytes. So this changes does not harm them.
 */
#define	BOOTSZ		440	/* size of boot code in master boot block */
#define	FD_NUMPART	4	/* number of 'partitions' in fdisk table */
#define	MBB_MAGIC	0xAA55	/* magic number for mboot.signature */
#define	DEFAULT_INTLV	4	/* default interleave for testing tracks */
#define	MINPSIZE	4	/* minimum number of cylinders in a partition */
#define	TSTPAT		0xE5	/* test pattern for verifying disk */

/*
 * structure to hold the fdisk partition table
 */
struct ipart {
	unsigned char bootid;	/* bootable or not */
	unsigned char beghead;	/* beginning head, sector, cylinder */
	unsigned char begsect;	/* begcyl is a 10-bit number. High 2 bits */
	unsigned char begcyl;	/*	are in begsect. */
	unsigned char systid;	/* OS type */
	unsigned char endhead;	/* ending head, sector, cylinder */
	unsigned char endsect;	/* endcyl is a 10-bit number.  High 2 bits */
	unsigned char endcyl;	/*	are in endsect. */
	uint32_t relsect;	/* first sector relative to start of disk */
	uint32_t numsect;	/* number of sectors in partition */
};
/*
 * Values for bootid.
 */
#define	NOTACTIVE	0
#define	ACTIVE		128
/*
 * Values for systid.
 */
#define	UNUSED		0	/* Empty Partition */
#define	DOSOS12		1	/* DOS partition, 12-bit FAT */
#define	PCIXOS		2	/* PC/IX partition */
#define	DOSOS16		4	/* DOS partition, 16-bit FAT */
#define	EXTDOS		5	/* EXT-DOS partition */
#define	DOSHUGE		6	/* Huge DOS partition  > 32MB */
#define	FDISK_IFS	7	/* Installable File System (IFS): HPFS & NTFS */
#define	FDISK_AIXBOOT	8	/* AIX Boot */
#define	FDISK_AIXDATA	9	/* AIX Data */
#define	FDISK_OS2BOOT	10	/* OS/2 Boot Manager */
#define	FDISK_WINDOWS	11	/* Windows 95 FAT32 (up to 2047GB) */
#define	FDISK_EXT_WIN	12	/* Windows 95 FAT32 (extended-INT13) */
#define	FDISK_FAT95	14	/* DOS 16-bit FAT, LBA-mapped */
#define	FDISK_EXTLBA	15	/* Extended partition, LBA-mapped */
#define	DIAGPART	18	/* Diagnostic boot partition (OS independent) */
#define	FDISK_LINUX	65	/* Linux */
#define	FDISK_LINUXDSWAP	66	/* Linux swap (sharing disk w/ DRDOS) */
#define	FDISK_LINUXDNAT	67	/* Linux native (sharing disk with DRDOS) */
#define	FDISK_CPM	82	/* CP/M */
#define	DOSDATA		86	/* DOS data partition */
#define	OTHEROS		98	/* part. type for appl. (DB?) needs */
				/* raw partition.  ID was 0 but conflicted */
				/* with DOS 3.3 fdisk    */
#define	UNIXOS		99	/* UNIX V.x partition */
#define	FDISK_NOVELL2	100	/* Novell Netware 286 */
#define	FDISK_NOVELL3	101	/* Novell Netware 3.x and later */
#define	FDISK_QNX4	119	/* QNX 4.x */
#define	FDISK_QNX42	120	/* QNX 4.x 2nd part */
#define	FDISK_QNX43	121	/* QNX 4.x 3rd part */
#define	SUNIXOS		130	/* Solaris UNIX partition */
#define	FDISK_LINUXNAT	131	/* Linux native */
#define	FDISK_NTFSVOL1	134	/* NTFS volume set 1 */
#define	FDISK_NTFSVOL2	135	/* NTFS volume set 2 */
#define	FDISK_BSD	165	/* BSD/386, 386BSD, NetBSD, FreeBSD, OpenBSD */
#define	FDISK_NEXTSTEP	167	/* NeXTSTEP */
#define	FDISK_BSDIFS	183	/* BSDI file system */
#define	FDISK_BSDISWAP	184	/* BSDI swap */
#define	X86BOOT		190	/* x86 Solaris boot partition */
#define	SUNIXOS2	191	/* Solaris UNIX partition */
#define	EFI_PMBR	238	/* EFI PMBR */
#define	EFI_FS		239	/* EFI File System (System Partition) */
#define	MAXDOS		65535L	/* max size (sectors) for DOS partition */

/*
 * structure to hold master boot block in physical sector 0 of the disk.
 * Note that partitions stuff can't be directly included in the structure
 * because of lameo '386 compiler alignment design.
 * Alignment issues also force us to have 2 16bit entities for a single
 * 32bit win_volserno. It is not used anywhere anyway.
 */

struct mboot {	/* master boot block */
	char	bootinst[BOOTSZ];
	uint16_t win_volserno_lo;
	uint16_t win_volserno_hi;
	uint16_t reserved;
	char	parts[FD_NUMPART * sizeof (struct ipart)];
	ushort_t signature;
};

#if defined(__i386) || defined(__amd64)

/* Byte offset of the start of the partition table within the sector */
#define	FDISK_PART_TABLE_START	446

/* Maximum number of valid partitions assumed as 32 */
#define	MAX_EXT_PARTS	32

#else

#define	MAX_EXT_PARTS	0

#endif	/* if defined(__i386) || defined(__amd64) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DKTP_FDISK_H */
