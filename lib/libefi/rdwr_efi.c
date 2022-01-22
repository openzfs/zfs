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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2012 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2018 by Delphix. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <zlib.h>
#include <libintl.h>
#include <sys/types.h>
#include <sys/dkio.h>
#include <sys/mhd.h>
#include <sys/param.h>
#include <sys/dktp/fdisk.h>
#include <sys/efi_partition.h>
#include <sys/byteorder.h>
#include <sys/vdev_disk.h>
#include <linux/fs.h>
#include <linux/blkpg.h>

static struct uuid_to_ptag {
	struct uuid	uuid;
} conversion_array[] = {
	{ EFI_UNUSED },
	{ EFI_BOOT },
	{ EFI_ROOT },
	{ EFI_SWAP },
	{ EFI_USR },
	{ EFI_BACKUP },
	{ EFI_UNUSED },		/* STAND is never used */
	{ EFI_VAR },
	{ EFI_HOME },
	{ EFI_ALTSCTR },
	{ EFI_UNUSED },		/* CACHE (cachefs) is never used */
	{ EFI_RESERVED },
	{ EFI_SYSTEM },
	{ EFI_LEGACY_MBR },
	{ EFI_SYMC_PUB },
	{ EFI_SYMC_CDS },
	{ EFI_MSFT_RESV },
	{ EFI_DELL_BASIC },
	{ EFI_DELL_RAID },
	{ EFI_DELL_SWAP },
	{ EFI_DELL_LVM },
	{ EFI_DELL_RESV },
	{ EFI_AAPL_HFS },
	{ EFI_AAPL_UFS },
	{ EFI_FREEBSD_BOOT },
	{ EFI_FREEBSD_SWAP },
	{ EFI_FREEBSD_UFS },
	{ EFI_FREEBSD_VINUM },
	{ EFI_FREEBSD_ZFS },
	{ EFI_BIOS_BOOT },
	{ EFI_INTC_RS },
	{ EFI_SNE_BOOT },
	{ EFI_LENOVO_BOOT },
	{ EFI_MSFT_LDMM },
	{ EFI_MSFT_LDMD },
	{ EFI_MSFT_RE },
	{ EFI_IBM_GPFS },
	{ EFI_MSFT_STORAGESPACES },
	{ EFI_HPQ_DATA },
	{ EFI_HPQ_SVC },
	{ EFI_RHT_DATA },
	{ EFI_RHT_HOME },
	{ EFI_RHT_SRV },
	{ EFI_RHT_DMCRYPT },
	{ EFI_RHT_LUKS },
	{ EFI_FREEBSD_DISKLABEL },
	{ EFI_AAPL_RAID },
	{ EFI_AAPL_RAIDOFFLINE },
	{ EFI_AAPL_BOOT },
	{ EFI_AAPL_LABEL },
	{ EFI_AAPL_TVRECOVERY },
	{ EFI_AAPL_CORESTORAGE },
	{ EFI_NETBSD_SWAP },
	{ EFI_NETBSD_FFS },
	{ EFI_NETBSD_LFS },
	{ EFI_NETBSD_RAID },
	{ EFI_NETBSD_CAT },
	{ EFI_NETBSD_CRYPT },
	{ EFI_GOOG_KERN },
	{ EFI_GOOG_ROOT },
	{ EFI_GOOG_RESV },
	{ EFI_HAIKU_BFS },
	{ EFI_MIDNIGHTBSD_BOOT },
	{ EFI_MIDNIGHTBSD_DATA },
	{ EFI_MIDNIGHTBSD_SWAP },
	{ EFI_MIDNIGHTBSD_UFS },
	{ EFI_MIDNIGHTBSD_VINUM },
	{ EFI_MIDNIGHTBSD_ZFS },
	{ EFI_CEPH_JOURNAL },
	{ EFI_CEPH_DMCRYPTJOURNAL },
	{ EFI_CEPH_OSD },
	{ EFI_CEPH_DMCRYPTOSD },
	{ EFI_CEPH_CREATE },
	{ EFI_CEPH_DMCRYPTCREATE },
	{ EFI_OPENBSD_DISKLABEL },
	{ EFI_BBRY_QNX },
	{ EFI_BELL_PLAN9 },
	{ EFI_VMW_KCORE },
	{ EFI_VMW_VMFS },
	{ EFI_VMW_RESV },
	{ EFI_RHT_ROOTX86 },
	{ EFI_RHT_ROOTAMD64 },
	{ EFI_RHT_ROOTARM },
	{ EFI_RHT_ROOTARM64 },
	{ EFI_ACRONIS_SECUREZONE },
	{ EFI_ONIE_BOOT },
	{ EFI_ONIE_CONFIG },
	{ EFI_IBM_PPRPBOOT },
	{ EFI_FREEDESKTOP_BOOT }
};

int efi_debug = 0;

static int efi_read(int, struct dk_gpt *);

/*
 * Return a 32-bit CRC of the contents of the buffer.  Pre-and-post
 * one's conditioning will be handled by crc32() internally.
 */
static uint32_t
efi_crc32(const unsigned char *buf, unsigned int size)
{
	uint32_t crc = crc32(0, Z_NULL, 0);

	crc = crc32(crc, buf, size);

	return (crc);
}

static int
read_disk_info(int fd, diskaddr_t *capacity, uint_t *lbsize)
{
	int sector_size;
	unsigned long long capacity_size;

	if (ioctl(fd, BLKSSZGET, &sector_size) < 0)
		return (-1);

	if (ioctl(fd, BLKGETSIZE64, &capacity_size) < 0)
		return (-1);

	*lbsize = (uint_t)sector_size;
	*capacity = (diskaddr_t)(capacity_size / sector_size);

	return (0);
}

/*
 * Return back the device name associated with the file descriptor. The
 * caller is responsible for freeing the memory associated with the
 * returned string.
 */
static char *
efi_get_devname(int fd)
{
	char path[32];

	/*
	 * The libefi API only provides the open fd and not the file path.
	 * To handle this realpath(3) is used to resolve the block device
	 * name from /proc/self/fd/<fd>.
	 */
	(void) snprintf(path, sizeof (path), "/proc/self/fd/%d", fd);
	return (realpath(path, NULL));
}

static int
efi_get_info(int fd, struct dk_cinfo *dki_info)
{
	char *dev_path;
	int rval = 0;

	memset(dki_info, 0, sizeof (*dki_info));

	/*
	 * The simplest way to get the partition number under linux is
	 * to parse it out of the /dev/<disk><partition> block device name.
	 * The kernel creates this using the partition number when it
	 * populates /dev/ so it may be trusted.  The tricky bit here is
	 * that the naming convention is based on the block device type.
	 * So we need to take this in to account when parsing out the
	 * partition information.  Aside from the partition number we collect
	 * some additional device info.
	 */
	dev_path = efi_get_devname(fd);
	if (dev_path == NULL)
		goto error;

	if ((strncmp(dev_path, "/dev/sd", 7) == 0)) {
		strcpy(dki_info->dki_cname, "sd");
		dki_info->dki_ctype = DKC_SCSI_CCS;
		rval = sscanf(dev_path, "/dev/%[a-zA-Z]%hu",
		    dki_info->dki_dname,
		    &dki_info->dki_partition);
	} else if ((strncmp(dev_path, "/dev/hd", 7) == 0)) {
		strcpy(dki_info->dki_cname, "hd");
		dki_info->dki_ctype = DKC_DIRECT;
		rval = sscanf(dev_path, "/dev/%[a-zA-Z]%hu",
		    dki_info->dki_dname,
		    &dki_info->dki_partition);
	} else if ((strncmp(dev_path, "/dev/md", 7) == 0)) {
		strcpy(dki_info->dki_cname, "pseudo");
		dki_info->dki_ctype = DKC_MD;
		strcpy(dki_info->dki_dname, "md");
		rval = sscanf(dev_path, "/dev/md%[0-9]p%hu",
		    dki_info->dki_dname + 2,
		    &dki_info->dki_partition);
	} else if ((strncmp(dev_path, "/dev/vd", 7) == 0)) {
		strcpy(dki_info->dki_cname, "vd");
		dki_info->dki_ctype = DKC_MD;
		rval = sscanf(dev_path, "/dev/%[a-zA-Z]%hu",
		    dki_info->dki_dname,
		    &dki_info->dki_partition);
	} else if ((strncmp(dev_path, "/dev/xvd", 8) == 0)) {
		strcpy(dki_info->dki_cname, "xvd");
		dki_info->dki_ctype = DKC_MD;
		rval = sscanf(dev_path, "/dev/%[a-zA-Z]%hu",
		    dki_info->dki_dname,
		    &dki_info->dki_partition);
	} else if ((strncmp(dev_path, "/dev/zd", 7) == 0)) {
		strcpy(dki_info->dki_cname, "zd");
		dki_info->dki_ctype = DKC_MD;
		strcpy(dki_info->dki_dname, "zd");
		rval = sscanf(dev_path, "/dev/zd%[0-9]p%hu",
		    dki_info->dki_dname + 2,
		    &dki_info->dki_partition);
	} else if ((strncmp(dev_path, "/dev/dm-", 8) == 0)) {
		strcpy(dki_info->dki_cname, "pseudo");
		dki_info->dki_ctype = DKC_VBD;
		strcpy(dki_info->dki_dname, "dm-");
		rval = sscanf(dev_path, "/dev/dm-%[0-9]p%hu",
		    dki_info->dki_dname + 3,
		    &dki_info->dki_partition);
	} else if ((strncmp(dev_path, "/dev/ram", 8) == 0)) {
		strcpy(dki_info->dki_cname, "pseudo");
		dki_info->dki_ctype = DKC_PCMCIA_MEM;
		strcpy(dki_info->dki_dname, "ram");
		rval = sscanf(dev_path, "/dev/ram%[0-9]p%hu",
		    dki_info->dki_dname + 3,
		    &dki_info->dki_partition);
	} else if ((strncmp(dev_path, "/dev/loop", 9) == 0)) {
		strcpy(dki_info->dki_cname, "pseudo");
		dki_info->dki_ctype = DKC_VBD;
		strcpy(dki_info->dki_dname, "loop");
		rval = sscanf(dev_path, "/dev/loop%[0-9]p%hu",
		    dki_info->dki_dname + 4,
		    &dki_info->dki_partition);
	} else if ((strncmp(dev_path, "/dev/nvme", 9) == 0)) {
		strcpy(dki_info->dki_cname, "nvme");
		dki_info->dki_ctype = DKC_SCSI_CCS;
		strcpy(dki_info->dki_dname, "nvme");
		(void) sscanf(dev_path, "/dev/nvme%[0-9]",
		    dki_info->dki_dname + 4);
		size_t controller_length = strlen(
		    dki_info->dki_dname);
		strcpy(dki_info->dki_dname + controller_length,
		    "n");
		rval = sscanf(dev_path,
		    "/dev/nvme%*[0-9]n%[0-9]p%hu",
		    dki_info->dki_dname + controller_length + 1,
		    &dki_info->dki_partition);
	} else {
		strcpy(dki_info->dki_dname, "unknown");
		strcpy(dki_info->dki_cname, "unknown");
		dki_info->dki_ctype = DKC_UNKNOWN;
	}

	switch (rval) {
	case 0:
		errno = EINVAL;
		goto error;
	case 1:
		dki_info->dki_partition = 0;
	}

	free(dev_path);

	return (0);
error:
	if (efi_debug)
		(void) fprintf(stderr, "DKIOCINFO errno 0x%x\n", errno);

	switch (errno) {
	case EIO:
		return (VT_EIO);
	case EINVAL:
		return (VT_EINVAL);
	default:
		return (VT_ERROR);
	}
}

/*
 * the number of blocks the EFI label takes up (round up to nearest
 * block)
 */
#define	NBLOCKS(p, l)	(1 + ((((p) * (int)sizeof (efi_gpe_t))  + \
				((l) - 1)) / (l)))
/* number of partitions -- limited by what we can malloc */
#define	MAX_PARTS	((4294967295UL - sizeof (struct dk_gpt)) / \
			    sizeof (struct dk_part))

int
efi_alloc_and_init(int fd, uint32_t nparts, struct dk_gpt **vtoc)
{
	diskaddr_t	capacity = 0;
	uint_t		lbsize = 0;
	uint_t		nblocks;
	size_t		length;
	struct dk_gpt	*vptr;
	struct uuid	uuid;
	struct dk_cinfo	dki_info;

	if (read_disk_info(fd, &capacity, &lbsize) != 0)
		return (-1);

	if (efi_get_info(fd, &dki_info) != 0)
		return (-1);

	if (dki_info.dki_partition != 0)
		return (-1);

	if ((dki_info.dki_ctype == DKC_PCMCIA_MEM) ||
	    (dki_info.dki_ctype == DKC_VBD) ||
	    (dki_info.dki_ctype == DKC_UNKNOWN))
		return (-1);

	nblocks = NBLOCKS(nparts, lbsize);
	if ((nblocks * lbsize) < EFI_MIN_ARRAY_SIZE + lbsize) {
		/* 16K plus one block for the GPT */
		nblocks = EFI_MIN_ARRAY_SIZE / lbsize + 1;
	}

	if (nparts > MAX_PARTS) {
		if (efi_debug) {
			(void) fprintf(stderr,
			"the maximum number of partitions supported is %lu\n",
			    MAX_PARTS);
		}
		return (-1);
	}

	length = sizeof (struct dk_gpt) +
	    sizeof (struct dk_part) * (nparts - 1);

	vptr = calloc(1, length);
	if (vptr == NULL)
		return (-1);

	*vtoc = vptr;

	vptr->efi_version = EFI_VERSION_CURRENT;
	vptr->efi_lbasize = lbsize;
	vptr->efi_nparts = nparts;
	/*
	 * add one block here for the PMBR; on disks with a 512 byte
	 * block size and 128 or fewer partitions, efi_first_u_lba
	 * should work out to "34"
	 */
	vptr->efi_first_u_lba = nblocks + 1;
	vptr->efi_last_lba = capacity - 1;
	vptr->efi_altern_lba = capacity -1;
	vptr->efi_last_u_lba = vptr->efi_last_lba - nblocks;

	(void) uuid_generate((uchar_t *)&uuid);
	UUID_LE_CONVERT(vptr->efi_disk_uguid, uuid);
	return (0);
}

/*
 * Read EFI - return partition number upon success.
 */
int
efi_alloc_and_read(int fd, struct dk_gpt **vtoc)
{
	int			rval;
	uint32_t		nparts;
	int			length;
	struct dk_gpt		*vptr;

	/* figure out the number of entries that would fit into 16K */
	nparts = EFI_MIN_ARRAY_SIZE / sizeof (efi_gpe_t);
	length = (int) sizeof (struct dk_gpt) +
	    (int) sizeof (struct dk_part) * (nparts - 1);
	vptr = calloc(1, length);

	if (vptr == NULL)
		return (VT_ERROR);

	vptr->efi_nparts = nparts;
	rval = efi_read(fd, vptr);

	if ((rval == VT_EINVAL) && vptr->efi_nparts > nparts) {
		void *tmp;
		length = (int) sizeof (struct dk_gpt) +
		    (int) sizeof (struct dk_part) * (vptr->efi_nparts - 1);
		nparts = vptr->efi_nparts;
		if ((tmp = realloc(vptr, length)) == NULL) {
			/* cppcheck-suppress doubleFree */
			free(vptr);
			*vtoc = NULL;
			return (VT_ERROR);
		} else {
			vptr = tmp;
			rval = efi_read(fd, vptr);
		}
	}

	if (rval < 0) {
		if (efi_debug) {
			(void) fprintf(stderr,
			    "read of EFI table failed, rval=%d\n", rval);
		}
		free(vptr);
		*vtoc = NULL;
	} else {
		*vtoc = vptr;
	}

	return (rval);
}

static int
efi_ioctl(int fd, int cmd, dk_efi_t *dk_ioc)
{
	void *data = dk_ioc->dki_data;
	int error;
	diskaddr_t capacity;
	uint_t lbsize;

	/*
	 * When the IO is not being performed in kernel as an ioctl we need
	 * to know the sector size so we can seek to the proper byte offset.
	 */
	if (read_disk_info(fd, &capacity, &lbsize) == -1) {
		if (efi_debug)
			fprintf(stderr, "unable to read disk info: %d", errno);

		errno = EIO;
		return (-1);
	}

	switch (cmd) {
	case DKIOCGETEFI:
		if (lbsize == 0) {
			if (efi_debug)
				(void) fprintf(stderr, "DKIOCGETEFI assuming "
				    "LBA %d bytes\n", DEV_BSIZE);

			lbsize = DEV_BSIZE;
		}

		error = lseek(fd, dk_ioc->dki_lba * lbsize, SEEK_SET);
		if (error == -1) {
			if (efi_debug)
				(void) fprintf(stderr, "DKIOCGETEFI lseek "
				    "error: %d\n", errno);
			return (error);
		}

		error = read(fd, data, dk_ioc->dki_length);
		if (error == -1) {
			if (efi_debug)
				(void) fprintf(stderr, "DKIOCGETEFI read "
				    "error: %d\n", errno);
			return (error);
		}

		if (error != dk_ioc->dki_length) {
			if (efi_debug)
				(void) fprintf(stderr, "DKIOCGETEFI short "
				    "read of %d bytes\n", error);
			errno = EIO;
			return (-1);
		}
		error = 0;
		break;

	case DKIOCSETEFI:
		if (lbsize == 0) {
			if (efi_debug)
				(void) fprintf(stderr, "DKIOCSETEFI unknown "
				    "LBA size\n");
			errno = EIO;
			return (-1);
		}

		error = lseek(fd, dk_ioc->dki_lba * lbsize, SEEK_SET);
		if (error == -1) {
			if (efi_debug)
				(void) fprintf(stderr, "DKIOCSETEFI lseek "
				    "error: %d\n", errno);
			return (error);
		}

		error = write(fd, data, dk_ioc->dki_length);
		if (error == -1) {
			if (efi_debug)
				(void) fprintf(stderr, "DKIOCSETEFI write "
				    "error: %d\n", errno);
			return (error);
		}

		if (error != dk_ioc->dki_length) {
			if (efi_debug)
				(void) fprintf(stderr, "DKIOCSETEFI short "
				    "write of %d bytes\n", error);
			errno = EIO;
			return (-1);
		}

		/* Sync the new EFI table to disk */
		error = fsync(fd);
		if (error == -1)
			return (error);

		/* Ensure any local disk cache is also flushed */
		if (ioctl(fd, BLKFLSBUF, 0) == -1)
			return (error);

		error = 0;
		break;

	default:
		if (efi_debug)
			(void) fprintf(stderr, "unsupported ioctl()\n");

		errno = EIO;
		return (-1);
	}

	return (error);
}

int
efi_rescan(int fd)
{
	int retry = 10;
	int error;

	/* Notify the kernel a devices partition table has been updated */
	while ((error = ioctl(fd, BLKRRPART)) != 0) {
		if ((--retry == 0) || (errno != EBUSY)) {
			(void) fprintf(stderr, "the kernel failed to rescan "
			    "the partition table: %d\n", errno);
			return (-1);
		}
		usleep(50000);
	}

	return (0);
}

static int
check_label(int fd, dk_efi_t *dk_ioc)
{
	efi_gpt_t		*efi;
	uint_t			crc;

	if (efi_ioctl(fd, DKIOCGETEFI, dk_ioc) == -1) {
		switch (errno) {
		case EIO:
			return (VT_EIO);
		default:
			return (VT_ERROR);
		}
	}
	efi = dk_ioc->dki_data;
	if (efi->efi_gpt_Signature != LE_64(EFI_SIGNATURE)) {
		if (efi_debug)
			(void) fprintf(stderr,
			    "Bad EFI signature: 0x%llx != 0x%llx\n",
			    (long long)efi->efi_gpt_Signature,
			    (long long)LE_64(EFI_SIGNATURE));
		return (VT_EINVAL);
	}

	/*
	 * check CRC of the header; the size of the header should
	 * never be larger than one block
	 */
	crc = efi->efi_gpt_HeaderCRC32;
	efi->efi_gpt_HeaderCRC32 = 0;
	len_t headerSize = (len_t)LE_32(efi->efi_gpt_HeaderSize);

	if (headerSize < EFI_MIN_LABEL_SIZE || headerSize > EFI_LABEL_SIZE) {
		if (efi_debug)
			(void) fprintf(stderr,
			    "Invalid EFI HeaderSize %llu.  Assuming %d.\n",
			    headerSize, EFI_MIN_LABEL_SIZE);
	}

	if ((headerSize > dk_ioc->dki_length) ||
	    crc != LE_32(efi_crc32((unsigned char *)efi, headerSize))) {
		if (efi_debug)
			(void) fprintf(stderr,
			    "Bad EFI CRC: 0x%x != 0x%x\n",
			    crc, LE_32(efi_crc32((unsigned char *)efi,
			    headerSize)));
		return (VT_EINVAL);
	}

	return (0);
}

static int
efi_read(int fd, struct dk_gpt *vtoc)
{
	int			i, j;
	int			label_len;
	int			rval = 0;
	int			md_flag = 0;
	int			vdc_flag = 0;
	diskaddr_t		capacity = 0;
	uint_t			lbsize = 0;
	struct dk_minfo		disk_info;
	dk_efi_t		dk_ioc;
	efi_gpt_t		*efi;
	efi_gpe_t		*efi_parts;
	struct dk_cinfo		dki_info;
	uint32_t		user_length;
	boolean_t		legacy_label = B_FALSE;

	/*
	 * get the partition number for this file descriptor.
	 */
	if ((rval = efi_get_info(fd, &dki_info)) != 0)
		return (rval);

	if ((strncmp(dki_info.dki_cname, "pseudo", 7) == 0) &&
	    (strncmp(dki_info.dki_dname, "md", 3) == 0)) {
		md_flag++;
	} else if ((strncmp(dki_info.dki_cname, "vdc", 4) == 0) &&
	    (strncmp(dki_info.dki_dname, "vdc", 4) == 0)) {
		/*
		 * The controller and drive name "vdc" (virtual disk client)
		 * indicates a LDoms virtual disk.
		 */
		vdc_flag++;
	}

	/* get the LBA size */
	if (read_disk_info(fd, &capacity, &lbsize) == -1) {
		if (efi_debug) {
			(void) fprintf(stderr,
			    "unable to read disk info: %d",
			    errno);
		}
		return (VT_EINVAL);
	}

	disk_info.dki_lbsize = lbsize;
	disk_info.dki_capacity = capacity;

	if (disk_info.dki_lbsize == 0) {
		if (efi_debug) {
			(void) fprintf(stderr,
			    "efi_read: assuming LBA 512 bytes\n");
		}
		disk_info.dki_lbsize = DEV_BSIZE;
	}
	/*
	 * Read the EFI GPT to figure out how many partitions we need
	 * to deal with.
	 */
	dk_ioc.dki_lba = 1;
	if (NBLOCKS(vtoc->efi_nparts, disk_info.dki_lbsize) < 34) {
		label_len = EFI_MIN_ARRAY_SIZE + disk_info.dki_lbsize;
	} else {
		label_len = vtoc->efi_nparts * (int) sizeof (efi_gpe_t) +
		    disk_info.dki_lbsize;
		if (label_len % disk_info.dki_lbsize) {
			/* pad to physical sector size */
			label_len += disk_info.dki_lbsize;
			label_len &= ~(disk_info.dki_lbsize - 1);
		}
	}

	if (posix_memalign((void **)&dk_ioc.dki_data,
	    disk_info.dki_lbsize, label_len))
		return (VT_ERROR);

	memset(dk_ioc.dki_data, 0, label_len);
	dk_ioc.dki_length = disk_info.dki_lbsize;
	user_length = vtoc->efi_nparts;
	efi = dk_ioc.dki_data;
	if (md_flag) {
		dk_ioc.dki_length = label_len;
		if (efi_ioctl(fd, DKIOCGETEFI, &dk_ioc) == -1) {
			switch (errno) {
			case EIO:
				return (VT_EIO);
			default:
				return (VT_ERROR);
			}
		}
	} else if ((rval = check_label(fd, &dk_ioc)) == VT_EINVAL) {
		/*
		 * No valid label here; try the alternate. Note that here
		 * we just read GPT header and save it into dk_ioc.data,
		 * Later, we will read GUID partition entry array if we
		 * can get valid GPT header.
		 */

		/*
		 * This is a workaround for legacy systems. In the past, the
		 * last sector of SCSI disk was invisible on x86 platform. At
		 * that time, backup label was saved on the next to the last
		 * sector. It is possible for users to move a disk from previous
		 * solaris system to present system. Here, we attempt to search
		 * legacy backup EFI label first.
		 */
		dk_ioc.dki_lba = disk_info.dki_capacity - 2;
		dk_ioc.dki_length = disk_info.dki_lbsize;
		rval = check_label(fd, &dk_ioc);
		if (rval == VT_EINVAL) {
			/*
			 * we didn't find legacy backup EFI label, try to
			 * search backup EFI label in the last block.
			 */
			dk_ioc.dki_lba = disk_info.dki_capacity - 1;
			dk_ioc.dki_length = disk_info.dki_lbsize;
			rval = check_label(fd, &dk_ioc);
			if (rval == 0) {
				legacy_label = B_TRUE;
				if (efi_debug)
					(void) fprintf(stderr,
					    "efi_read: primary label corrupt; "
					    "using EFI backup label located on"
					    " the last block\n");
			}
		} else {
			if ((efi_debug) && (rval == 0))
				(void) fprintf(stderr, "efi_read: primary label"
				    " corrupt; using legacy EFI backup label "
				    " located on the next to last block\n");
		}

		if (rval == 0) {
			dk_ioc.dki_lba = LE_64(efi->efi_gpt_PartitionEntryLBA);
			vtoc->efi_flags |= EFI_GPT_PRIMARY_CORRUPT;
			vtoc->efi_nparts =
			    LE_32(efi->efi_gpt_NumberOfPartitionEntries);
			/*
			 * Partition tables are between backup GPT header
			 * table and ParitionEntryLBA (the starting LBA of
			 * the GUID partition entries array). Now that we
			 * already got valid GPT header and saved it in
			 * dk_ioc.dki_data, we try to get GUID partition
			 * entry array here.
			 */
			/* LINTED */
			dk_ioc.dki_data = (efi_gpt_t *)((char *)dk_ioc.dki_data
			    + disk_info.dki_lbsize);
			if (legacy_label)
				dk_ioc.dki_length = disk_info.dki_capacity - 1 -
				    dk_ioc.dki_lba;
			else
				dk_ioc.dki_length = disk_info.dki_capacity - 2 -
				    dk_ioc.dki_lba;
			dk_ioc.dki_length *= disk_info.dki_lbsize;
			if (dk_ioc.dki_length >
			    ((len_t)label_len - sizeof (*dk_ioc.dki_data))) {
				rval = VT_EINVAL;
			} else {
				/*
				 * read GUID partition entry array
				 */
				rval = efi_ioctl(fd, DKIOCGETEFI, &dk_ioc);
			}
		}

	} else if (rval == 0) {

		dk_ioc.dki_lba = LE_64(efi->efi_gpt_PartitionEntryLBA);
		/* LINTED */
		dk_ioc.dki_data = (efi_gpt_t *)((char *)dk_ioc.dki_data
		    + disk_info.dki_lbsize);
		dk_ioc.dki_length = label_len - disk_info.dki_lbsize;
		rval = efi_ioctl(fd, DKIOCGETEFI, &dk_ioc);

	} else if (vdc_flag && rval == VT_ERROR && errno == EINVAL) {
		/*
		 * When the device is a LDoms virtual disk, the DKIOCGETEFI
		 * ioctl can fail with EINVAL if the virtual disk backend
		 * is a ZFS volume serviced by a domain running an old version
		 * of Solaris. This is because the DKIOCGETEFI ioctl was
		 * initially incorrectly implemented for a ZFS volume and it
		 * expected the GPT and GPE to be retrieved with a single ioctl.
		 * So we try to read the GPT and the GPE using that old style
		 * ioctl.
		 */
		dk_ioc.dki_lba = 1;
		dk_ioc.dki_length = label_len;
		rval = check_label(fd, &dk_ioc);
	}

	if (rval < 0) {
		free(efi);
		return (rval);
	}

	/* LINTED -- always longlong aligned */
	efi_parts = (efi_gpe_t *)(((char *)efi) + disk_info.dki_lbsize);

	/*
	 * Assemble this into a "dk_gpt" struct for easier
	 * digestibility by applications.
	 */
	vtoc->efi_version = LE_32(efi->efi_gpt_Revision);
	vtoc->efi_nparts = LE_32(efi->efi_gpt_NumberOfPartitionEntries);
	vtoc->efi_part_size = LE_32(efi->efi_gpt_SizeOfPartitionEntry);
	vtoc->efi_lbasize = disk_info.dki_lbsize;
	vtoc->efi_last_lba = disk_info.dki_capacity - 1;
	vtoc->efi_first_u_lba = LE_64(efi->efi_gpt_FirstUsableLBA);
	vtoc->efi_last_u_lba = LE_64(efi->efi_gpt_LastUsableLBA);
	vtoc->efi_altern_lba = LE_64(efi->efi_gpt_AlternateLBA);
	UUID_LE_CONVERT(vtoc->efi_disk_uguid, efi->efi_gpt_DiskGUID);

	/*
	 * If the array the user passed in is too small, set the length
	 * to what it needs to be and return
	 */
	if (user_length < vtoc->efi_nparts) {
		return (VT_EINVAL);
	}

	for (i = 0; i < vtoc->efi_nparts; i++) {
		UUID_LE_CONVERT(vtoc->efi_parts[i].p_guid,
		    efi_parts[i].efi_gpe_PartitionTypeGUID);

		for (j = 0;
		    j < sizeof (conversion_array)
		    / sizeof (struct uuid_to_ptag); j++) {

			if (memcmp(&vtoc->efi_parts[i].p_guid,
			    &conversion_array[j].uuid,
			    sizeof (struct uuid)) == 0) {
				vtoc->efi_parts[i].p_tag = j;
				break;
			}
		}
		if (vtoc->efi_parts[i].p_tag == V_UNASSIGNED)
			continue;
		vtoc->efi_parts[i].p_flag =
		    LE_16(efi_parts[i].efi_gpe_Attributes.PartitionAttrs);
		vtoc->efi_parts[i].p_start =
		    LE_64(efi_parts[i].efi_gpe_StartingLBA);
		vtoc->efi_parts[i].p_size =
		    LE_64(efi_parts[i].efi_gpe_EndingLBA) -
		    vtoc->efi_parts[i].p_start + 1;
		for (j = 0; j < EFI_PART_NAME_LEN; j++) {
			vtoc->efi_parts[i].p_name[j] =
			    (uchar_t)LE_16(
			    efi_parts[i].efi_gpe_PartitionName[j]);
		}

		UUID_LE_CONVERT(vtoc->efi_parts[i].p_uguid,
		    efi_parts[i].efi_gpe_UniquePartitionGUID);
	}
	free(efi);

	return (dki_info.dki_partition);
}

/* writes a "protective" MBR */
static int
write_pmbr(int fd, struct dk_gpt *vtoc)
{
	dk_efi_t	dk_ioc;
	struct mboot	mb;
	uchar_t		*cp;
	diskaddr_t	size_in_lba;
	uchar_t		*buf;
	int		len;

	len = (vtoc->efi_lbasize == 0) ? sizeof (mb) : vtoc->efi_lbasize;
	if (posix_memalign((void **)&buf, len, len))
		return (VT_ERROR);

	/*
	 * Preserve any boot code and disk signature if the first block is
	 * already an MBR.
	 */
	memset(buf, 0, len);
	dk_ioc.dki_lba = 0;
	dk_ioc.dki_length = len;
	/* LINTED -- always longlong aligned */
	dk_ioc.dki_data = (efi_gpt_t *)buf;
	if (efi_ioctl(fd, DKIOCGETEFI, &dk_ioc) == -1) {
		memset(&mb, 0, sizeof (mb));
		mb.signature = LE_16(MBB_MAGIC);
	} else {
		(void) memcpy(&mb, buf, sizeof (mb));
		if (mb.signature != LE_16(MBB_MAGIC)) {
			memset(&mb, 0, sizeof (mb));
			mb.signature = LE_16(MBB_MAGIC);
		}
	}

	memset(&mb.parts, 0, sizeof (mb.parts));
	cp = (uchar_t *)&mb.parts[0];
	/* bootable or not */
	*cp++ = 0;
	/* beginning CHS; 0xffffff if not representable */
	*cp++ = 0xff;
	*cp++ = 0xff;
	*cp++ = 0xff;
	/* OS type */
	*cp++ = EFI_PMBR;
	/* ending CHS; 0xffffff if not representable */
	*cp++ = 0xff;
	*cp++ = 0xff;
	*cp++ = 0xff;
	/* starting LBA: 1 (little endian format) by EFI definition */
	*cp++ = 0x01;
	*cp++ = 0x00;
	*cp++ = 0x00;
	*cp++ = 0x00;
	/* ending LBA: last block on the disk (little endian format) */
	size_in_lba = vtoc->efi_last_lba;
	if (size_in_lba < 0xffffffff) {
		*cp++ = (size_in_lba & 0x000000ff);
		*cp++ = (size_in_lba & 0x0000ff00) >> 8;
		*cp++ = (size_in_lba & 0x00ff0000) >> 16;
		*cp++ = (size_in_lba & 0xff000000) >> 24;
	} else {
		*cp++ = 0xff;
		*cp++ = 0xff;
		*cp++ = 0xff;
		*cp++ = 0xff;
	}

	(void) memcpy(buf, &mb, sizeof (mb));
	/* LINTED -- always longlong aligned */
	dk_ioc.dki_data = (efi_gpt_t *)buf;
	dk_ioc.dki_lba = 0;
	dk_ioc.dki_length = len;
	if (efi_ioctl(fd, DKIOCSETEFI, &dk_ioc) == -1) {
		free(buf);
		switch (errno) {
		case EIO:
			return (VT_EIO);
		case EINVAL:
			return (VT_EINVAL);
		default:
			return (VT_ERROR);
		}
	}
	free(buf);
	return (0);
}

/* make sure the user specified something reasonable */
static int
check_input(struct dk_gpt *vtoc)
{
	int			resv_part = -1;
	int			i, j;
	diskaddr_t		istart, jstart, isize, jsize, endsect;

	/*
	 * Sanity-check the input (make sure no partitions overlap)
	 */
	for (i = 0; i < vtoc->efi_nparts; i++) {
		/* It can't be unassigned and have an actual size */
		if ((vtoc->efi_parts[i].p_tag == V_UNASSIGNED) &&
		    (vtoc->efi_parts[i].p_size != 0)) {
			if (efi_debug) {
				(void) fprintf(stderr, "partition %d is "
				    "\"unassigned\" but has a size of %llu",
				    i, vtoc->efi_parts[i].p_size);
			}
			return (VT_EINVAL);
		}
		if (vtoc->efi_parts[i].p_tag == V_UNASSIGNED) {
			if (uuid_is_null((uchar_t *)&vtoc->efi_parts[i].p_guid))
				continue;
			/* we have encountered an unknown uuid */
			vtoc->efi_parts[i].p_tag = 0xff;
		}
		if (vtoc->efi_parts[i].p_tag == V_RESERVED) {
			if (resv_part != -1) {
				if (efi_debug) {
					(void) fprintf(stderr, "found "
					    "duplicate reserved partition "
					    "at %d\n", i);
				}
				return (VT_EINVAL);
			}
			resv_part = i;
		}
		if ((vtoc->efi_parts[i].p_start < vtoc->efi_first_u_lba) ||
		    (vtoc->efi_parts[i].p_start > vtoc->efi_last_u_lba)) {
			if (efi_debug) {
				(void) fprintf(stderr,
				    "Partition %d starts at %llu.  ",
				    i,
				    vtoc->efi_parts[i].p_start);
				(void) fprintf(stderr,
				    "It must be between %llu and %llu.\n",
				    vtoc->efi_first_u_lba,
				    vtoc->efi_last_u_lba);
			}
			return (VT_EINVAL);
		}
		if ((vtoc->efi_parts[i].p_start +
		    vtoc->efi_parts[i].p_size <
		    vtoc->efi_first_u_lba) ||
		    (vtoc->efi_parts[i].p_start +
		    vtoc->efi_parts[i].p_size >
		    vtoc->efi_last_u_lba + 1)) {
			if (efi_debug) {
				(void) fprintf(stderr,
				    "Partition %d ends at %llu.  ",
				    i,
				    vtoc->efi_parts[i].p_start +
				    vtoc->efi_parts[i].p_size);
				(void) fprintf(stderr,
				    "It must be between %llu and %llu.\n",
				    vtoc->efi_first_u_lba,
				    vtoc->efi_last_u_lba);
			}
			return (VT_EINVAL);
		}

		for (j = 0; j < vtoc->efi_nparts; j++) {
			isize = vtoc->efi_parts[i].p_size;
			jsize = vtoc->efi_parts[j].p_size;
			istart = vtoc->efi_parts[i].p_start;
			jstart = vtoc->efi_parts[j].p_start;
			if ((i != j) && (isize != 0) && (jsize != 0)) {
				endsect = jstart + jsize -1;
				if ((jstart <= istart) &&
				    (istart <= endsect)) {
					if (efi_debug) {
						(void) fprintf(stderr,
						    "Partition %d overlaps "
						    "partition %d.", i, j);
					}
					return (VT_EINVAL);
				}
			}
		}
	}
	/* just a warning for now */
	if ((resv_part == -1) && efi_debug) {
		(void) fprintf(stderr,
		    "no reserved partition found\n");
	}
	return (0);
}

static int
call_blkpg_ioctl(int fd, int command, diskaddr_t start,
    diskaddr_t size, uint_t pno)
{
	struct blkpg_ioctl_arg ioctl_arg;
	struct blkpg_partition  linux_part;
	memset(&linux_part, 0, sizeof (linux_part));

	char *path = efi_get_devname(fd);
	if (path == NULL) {
		(void) fprintf(stderr, "failed to retrieve device name\n");
		return (VT_EINVAL);
	}

	linux_part.start = start;
	linux_part.length = size;
	linux_part.pno = pno;
	snprintf(linux_part.devname, BLKPG_DEVNAMELTH - 1, "%s%u", path, pno);
	linux_part.devname[BLKPG_DEVNAMELTH - 1] = '\0';
	free(path);

	ioctl_arg.op = command;
	ioctl_arg.flags = 0;
	ioctl_arg.datalen = sizeof (struct blkpg_partition);
	ioctl_arg.data = &linux_part;

	return (ioctl(fd, BLKPG, &ioctl_arg));
}

/*
 * add all the unallocated space to the current label
 */
int
efi_use_whole_disk(int fd)
{
	struct dk_gpt *efi_label = NULL;
	int rval;
	int i;
	uint_t resv_index = 0, data_index = 0;
	diskaddr_t resv_start = 0, data_start = 0;
	diskaddr_t data_size, limit, difference;
	boolean_t sync_needed = B_FALSE;
	uint_t nblocks;

	rval = efi_alloc_and_read(fd, &efi_label);
	if (rval < 0) {
		if (efi_label != NULL)
			efi_free(efi_label);
		return (rval);
	}

	/*
	 * Find the last physically non-zero partition.
	 * This should be the reserved partition.
	 */
	for (i = 0; i < efi_label->efi_nparts; i ++) {
		if (resv_start < efi_label->efi_parts[i].p_start) {
			resv_start = efi_label->efi_parts[i].p_start;
			resv_index = i;
		}
	}

	/*
	 * Find the last physically non-zero partition before that.
	 * This is the data partition.
	 */
	for (i = 0; i < resv_index; i ++) {
		if (data_start < efi_label->efi_parts[i].p_start) {
			data_start = efi_label->efi_parts[i].p_start;
			data_index = i;
		}
	}
	data_size = efi_label->efi_parts[data_index].p_size;

	/*
	 * See the "efi_alloc_and_init" function for more information
	 * about where this "nblocks" value comes from.
	 */
	nblocks = efi_label->efi_first_u_lba - 1;

	/*
	 * Determine if the EFI label is out of sync. We check that:
	 *
	 * 1. the data partition ends at the limit we set, and
	 * 2. the reserved partition starts at the limit we set.
	 *
	 * If either of these conditions is not met, then we need to
	 * resync the EFI label.
	 *
	 * The limit is the last usable LBA, determined by the last LBA
	 * and the first usable LBA fields on the EFI label of the disk
	 * (see the lines directly above). Additionally, we factor in
	 * EFI_MIN_RESV_SIZE (per its use in "zpool_label_disk") and
	 * P2ALIGN it to ensure the partition boundaries are aligned
	 * (for performance reasons). The alignment should match the
	 * alignment used by the "zpool_label_disk" function.
	 */
	limit = P2ALIGN(efi_label->efi_last_lba - nblocks - EFI_MIN_RESV_SIZE,
	    PARTITION_END_ALIGNMENT);
	if (data_start + data_size != limit || resv_start != limit)
		sync_needed = B_TRUE;

	if (efi_debug && sync_needed)
		(void) fprintf(stderr, "efi_use_whole_disk: sync needed\n");

	/*
	 * If alter_lba is 1, we are using the backup label.
	 * Since we can locate the backup label by disk capacity,
	 * there must be no unallocated space.
	 */
	if ((efi_label->efi_altern_lba == 1) || (efi_label->efi_altern_lba
	    >= efi_label->efi_last_lba && !sync_needed)) {
		if (efi_debug) {
			(void) fprintf(stderr,
			    "efi_use_whole_disk: requested space not found\n");
		}
		efi_free(efi_label);
		return (VT_ENOSPC);
	}

	/*
	 * Verify that we've found the reserved partition by checking
	 * that it looks the way it did when we created it in zpool_label_disk.
	 * If we've found the incorrect partition, then we know that this
	 * device was reformatted and no longer is solely used by ZFS.
	 */
	if ((efi_label->efi_parts[resv_index].p_size != EFI_MIN_RESV_SIZE) ||
	    (efi_label->efi_parts[resv_index].p_tag != V_RESERVED) ||
	    (resv_index != 8)) {
		if (efi_debug) {
			(void) fprintf(stderr,
			    "efi_use_whole_disk: wholedisk not available\n");
		}
		efi_free(efi_label);
		return (VT_ENOSPC);
	}

	if (data_start + data_size != resv_start) {
		if (efi_debug) {
			(void) fprintf(stderr,
			    "efi_use_whole_disk: "
			    "data_start (%lli) + "
			    "data_size (%lli) != "
			    "resv_start (%lli)\n",
			    data_start, data_size, resv_start);
		}

		return (VT_EINVAL);
	}

	if (limit < resv_start) {
		if (efi_debug) {
			(void) fprintf(stderr,
			    "efi_use_whole_disk: "
			    "limit (%lli) < resv_start (%lli)\n",
			    limit, resv_start);
		}

		return (VT_EINVAL);
	}

	difference = limit - resv_start;

	if (efi_debug)
		(void) fprintf(stderr,
		    "efi_use_whole_disk: difference is %lli\n", difference);

	/*
	 * Move the reserved partition. There is currently no data in
	 * here except fabricated devids (which get generated via
	 * efi_write()). So there is no need to copy data.
	 */
	efi_label->efi_parts[data_index].p_size += difference;
	efi_label->efi_parts[resv_index].p_start += difference;
	efi_label->efi_last_u_lba = efi_label->efi_last_lba - nblocks;

	/*
	 * Rescanning the partition table in the kernel can result
	 * in the device links to be removed (see comment in vdev_disk_open).
	 * If BLKPG_RESIZE_PARTITION is available, then we can resize
	 * the partition table online and avoid having to remove the device
	 * links used by the pool. This provides a very deterministic
	 * approach to resizing devices and does not require any
	 * loops waiting for devices to reappear.
	 */
#ifdef BLKPG_RESIZE_PARTITION
	/*
	 * Delete the reserved partition since we're about to expand
	 * the data partition and it would overlap with the reserved
	 * partition.
	 * NOTE: The starting index for the ioctl is 1 while for the
	 * EFI partitions it's 0. For that reason we have to add one
	 * whenever we make an ioctl call.
	 */
	rval = call_blkpg_ioctl(fd, BLKPG_DEL_PARTITION, 0, 0, resv_index + 1);
	if (rval != 0)
		goto out;

	/*
	 * Expand the data partition
	 */
	rval = call_blkpg_ioctl(fd, BLKPG_RESIZE_PARTITION,
	    efi_label->efi_parts[data_index].p_start * efi_label->efi_lbasize,
	    efi_label->efi_parts[data_index].p_size * efi_label->efi_lbasize,
	    data_index + 1);
	if (rval != 0) {
		(void) fprintf(stderr, "Unable to resize data "
		    "partition:  %d\n", rval);
		/*
		 * Since we failed to resize, we need to reset the start
		 * of the reserve partition and re-create it.
		 */
		efi_label->efi_parts[resv_index].p_start -= difference;
	}

	/*
	 * Re-add the reserved partition. If we've expanded the data partition
	 * then we'll move the reserve partition to the end of the data
	 * partition. Otherwise, we'll recreate the partition in its original
	 * location. Note that we do this as best-effort and ignore any
	 * errors that may arise here. This will ensure that we finish writing
	 * the EFI label.
	 */
	(void) call_blkpg_ioctl(fd, BLKPG_ADD_PARTITION,
	    efi_label->efi_parts[resv_index].p_start * efi_label->efi_lbasize,
	    efi_label->efi_parts[resv_index].p_size * efi_label->efi_lbasize,
	    resv_index + 1);
#endif

	/*
	 * We're now ready to write the EFI label.
	 */
	if (rval == 0) {
		rval = efi_write(fd, efi_label);
		if (rval < 0 && efi_debug) {
			(void) fprintf(stderr, "efi_use_whole_disk:fail "
			    "to write label, rval=%d\n", rval);
		}
	}

out:
	efi_free(efi_label);
	return (rval);
}

/*
 * write EFI label and backup label
 */
int
efi_write(int fd, struct dk_gpt *vtoc)
{
	dk_efi_t		dk_ioc;
	efi_gpt_t		*efi;
	efi_gpe_t		*efi_parts;
	int			i, j;
	struct dk_cinfo		dki_info;
	int			rval;
	int			md_flag = 0;
	int			nblocks;
	diskaddr_t		lba_backup_gpt_hdr;

	if ((rval = efi_get_info(fd, &dki_info)) != 0)
		return (rval);

	/* check if we are dealing with a metadevice */
	if ((strncmp(dki_info.dki_cname, "pseudo", 7) == 0) &&
	    (strncmp(dki_info.dki_dname, "md", 3) == 0)) {
		md_flag = 1;
	}

	if (check_input(vtoc)) {
		/*
		 * not valid; if it's a metadevice just pass it down
		 * because SVM will do its own checking
		 */
		if (md_flag == 0) {
			return (VT_EINVAL);
		}
	}

	dk_ioc.dki_lba = 1;
	if (NBLOCKS(vtoc->efi_nparts, vtoc->efi_lbasize) < 34) {
		dk_ioc.dki_length = EFI_MIN_ARRAY_SIZE + vtoc->efi_lbasize;
	} else {
		dk_ioc.dki_length = NBLOCKS(vtoc->efi_nparts,
		    vtoc->efi_lbasize) *
		    vtoc->efi_lbasize;
	}

	/*
	 * the number of blocks occupied by GUID partition entry array
	 */
	nblocks = dk_ioc.dki_length / vtoc->efi_lbasize - 1;

	/*
	 * Backup GPT header is located on the block after GUID
	 * partition entry array. Here, we calculate the address
	 * for backup GPT header.
	 */
	lba_backup_gpt_hdr = vtoc->efi_last_u_lba + 1 + nblocks;
	if (posix_memalign((void **)&dk_ioc.dki_data,
	    vtoc->efi_lbasize, dk_ioc.dki_length))
		return (VT_ERROR);

	memset(dk_ioc.dki_data, 0, dk_ioc.dki_length);
	efi = dk_ioc.dki_data;

	/* stuff user's input into EFI struct */
	efi->efi_gpt_Signature = LE_64(EFI_SIGNATURE);
	efi->efi_gpt_Revision = LE_32(vtoc->efi_version); /* 0x02000100 */
	efi->efi_gpt_HeaderSize = LE_32(sizeof (struct efi_gpt) - LEN_EFI_PAD);
	efi->efi_gpt_Reserved1 = 0;
	efi->efi_gpt_MyLBA = LE_64(1ULL);
	efi->efi_gpt_AlternateLBA = LE_64(lba_backup_gpt_hdr);
	efi->efi_gpt_FirstUsableLBA = LE_64(vtoc->efi_first_u_lba);
	efi->efi_gpt_LastUsableLBA = LE_64(vtoc->efi_last_u_lba);
	efi->efi_gpt_PartitionEntryLBA = LE_64(2ULL);
	efi->efi_gpt_NumberOfPartitionEntries = LE_32(vtoc->efi_nparts);
	efi->efi_gpt_SizeOfPartitionEntry = LE_32(sizeof (struct efi_gpe));
	UUID_LE_CONVERT(efi->efi_gpt_DiskGUID, vtoc->efi_disk_uguid);

	/* LINTED -- always longlong aligned */
	efi_parts = (efi_gpe_t *)((char *)dk_ioc.dki_data + vtoc->efi_lbasize);

	for (i = 0; i < vtoc->efi_nparts; i++) {
		for (j = 0;
		    j < sizeof (conversion_array) /
		    sizeof (struct uuid_to_ptag); j++) {

			if (vtoc->efi_parts[i].p_tag == j) {
				UUID_LE_CONVERT(
				    efi_parts[i].efi_gpe_PartitionTypeGUID,
				    conversion_array[j].uuid);
				break;
			}
		}

		if (j == sizeof (conversion_array) /
		    sizeof (struct uuid_to_ptag)) {
			/*
			 * If we didn't have a matching uuid match, bail here.
			 * Don't write a label with unknown uuid.
			 */
			if (efi_debug) {
				(void) fprintf(stderr,
				    "Unknown uuid for p_tag %d\n",
				    vtoc->efi_parts[i].p_tag);
			}
			return (VT_EINVAL);
		}

		/* Zero's should be written for empty partitions */
		if (vtoc->efi_parts[i].p_tag == V_UNASSIGNED)
			continue;

		efi_parts[i].efi_gpe_StartingLBA =
		    LE_64(vtoc->efi_parts[i].p_start);
		efi_parts[i].efi_gpe_EndingLBA =
		    LE_64(vtoc->efi_parts[i].p_start +
		    vtoc->efi_parts[i].p_size - 1);
		efi_parts[i].efi_gpe_Attributes.PartitionAttrs =
		    LE_16(vtoc->efi_parts[i].p_flag);
		for (j = 0; j < EFI_PART_NAME_LEN; j++) {
			efi_parts[i].efi_gpe_PartitionName[j] =
			    LE_16((ushort_t)vtoc->efi_parts[i].p_name[j]);
		}
		if ((vtoc->efi_parts[i].p_tag != V_UNASSIGNED) &&
		    uuid_is_null((uchar_t *)&vtoc->efi_parts[i].p_uguid)) {
			(void) uuid_generate((uchar_t *)
			    &vtoc->efi_parts[i].p_uguid);
		}
		memcpy(&efi_parts[i].efi_gpe_UniquePartitionGUID,
		    &vtoc->efi_parts[i].p_uguid,
		    sizeof (uuid_t));
	}
	efi->efi_gpt_PartitionEntryArrayCRC32 =
	    LE_32(efi_crc32((unsigned char *)efi_parts,
	    vtoc->efi_nparts * (int)sizeof (struct efi_gpe)));
	efi->efi_gpt_HeaderCRC32 =
	    LE_32(efi_crc32((unsigned char *)efi,
	    LE_32(efi->efi_gpt_HeaderSize)));

	if (efi_ioctl(fd, DKIOCSETEFI, &dk_ioc) == -1) {
		free(dk_ioc.dki_data);
		switch (errno) {
		case EIO:
			return (VT_EIO);
		case EINVAL:
			return (VT_EINVAL);
		default:
			return (VT_ERROR);
		}
	}
	/* if it's a metadevice we're done */
	if (md_flag) {
		free(dk_ioc.dki_data);
		return (0);
	}

	/* write backup partition array */
	dk_ioc.dki_lba = vtoc->efi_last_u_lba + 1;
	dk_ioc.dki_length -= vtoc->efi_lbasize;
	/* LINTED */
	dk_ioc.dki_data = (efi_gpt_t *)((char *)dk_ioc.dki_data +
	    vtoc->efi_lbasize);

	if (efi_ioctl(fd, DKIOCSETEFI, &dk_ioc) == -1) {
		/*
		 * we wrote the primary label okay, so don't fail
		 */
		if (efi_debug) {
			(void) fprintf(stderr,
			    "write of backup partitions to block %llu "
			    "failed, errno %d\n",
			    vtoc->efi_last_u_lba + 1,
			    errno);
		}
	}
	/*
	 * now swap MyLBA and AlternateLBA fields and write backup
	 * partition table header
	 */
	dk_ioc.dki_lba = lba_backup_gpt_hdr;
	dk_ioc.dki_length = vtoc->efi_lbasize;
	/* LINTED */
	dk_ioc.dki_data = (efi_gpt_t *)((char *)dk_ioc.dki_data -
	    vtoc->efi_lbasize);
	efi->efi_gpt_AlternateLBA = LE_64(1ULL);
	efi->efi_gpt_MyLBA = LE_64(lba_backup_gpt_hdr);
	efi->efi_gpt_PartitionEntryLBA = LE_64(vtoc->efi_last_u_lba + 1);
	efi->efi_gpt_HeaderCRC32 = 0;
	efi->efi_gpt_HeaderCRC32 =
	    LE_32(efi_crc32((unsigned char *)dk_ioc.dki_data,
	    LE_32(efi->efi_gpt_HeaderSize)));

	if (efi_ioctl(fd, DKIOCSETEFI, &dk_ioc) == -1) {
		if (efi_debug) {
			(void) fprintf(stderr,
			    "write of backup header to block %llu failed, "
			    "errno %d\n",
			    lba_backup_gpt_hdr,
			    errno);
		}
	}
	/* write the PMBR */
	(void) write_pmbr(fd, vtoc);
	free(dk_ioc.dki_data);

	return (0);
}

void
efi_free(struct dk_gpt *ptr)
{
	free(ptr);
}

void
efi_err_check(struct dk_gpt *vtoc)
{
	int			resv_part = -1;
	int			i, j;
	diskaddr_t		istart, jstart, isize, jsize, endsect;
	int			overlap = 0;

	/*
	 * make sure no partitions overlap
	 */
	for (i = 0; i < vtoc->efi_nparts; i++) {
		/* It can't be unassigned and have an actual size */
		if ((vtoc->efi_parts[i].p_tag == V_UNASSIGNED) &&
		    (vtoc->efi_parts[i].p_size != 0)) {
			(void) fprintf(stderr,
			    "partition %d is \"unassigned\" but has a size "
			    "of %llu\n", i, vtoc->efi_parts[i].p_size);
		}
		if (vtoc->efi_parts[i].p_tag == V_UNASSIGNED) {
			continue;
		}
		if (vtoc->efi_parts[i].p_tag == V_RESERVED) {
			if (resv_part != -1) {
				(void) fprintf(stderr,
				    "found duplicate reserved partition at "
				    "%d\n", i);
			}
			resv_part = i;
			if (vtoc->efi_parts[i].p_size != EFI_MIN_RESV_SIZE)
				(void) fprintf(stderr,
				    "Warning: reserved partition size must "
				    "be %d sectors\n", EFI_MIN_RESV_SIZE);
		}
		if ((vtoc->efi_parts[i].p_start < vtoc->efi_first_u_lba) ||
		    (vtoc->efi_parts[i].p_start > vtoc->efi_last_u_lba)) {
			(void) fprintf(stderr,
			    "Partition %d starts at %llu\n",
			    i,
			    vtoc->efi_parts[i].p_start);
			(void) fprintf(stderr,
			    "It must be between %llu and %llu.\n",
			    vtoc->efi_first_u_lba,
			    vtoc->efi_last_u_lba);
		}
		if ((vtoc->efi_parts[i].p_start +
		    vtoc->efi_parts[i].p_size <
		    vtoc->efi_first_u_lba) ||
		    (vtoc->efi_parts[i].p_start +
		    vtoc->efi_parts[i].p_size >
		    vtoc->efi_last_u_lba + 1)) {
			(void) fprintf(stderr,
			    "Partition %d ends at %llu\n",
			    i,
			    vtoc->efi_parts[i].p_start +
			    vtoc->efi_parts[i].p_size);
			(void) fprintf(stderr,
			    "It must be between %llu and %llu.\n",
			    vtoc->efi_first_u_lba,
			    vtoc->efi_last_u_lba);
		}

		for (j = 0; j < vtoc->efi_nparts; j++) {
			isize = vtoc->efi_parts[i].p_size;
			jsize = vtoc->efi_parts[j].p_size;
			istart = vtoc->efi_parts[i].p_start;
			jstart = vtoc->efi_parts[j].p_start;
			if ((i != j) && (isize != 0) && (jsize != 0)) {
				endsect = jstart + jsize -1;
				if ((jstart <= istart) &&
				    (istart <= endsect)) {
					if (!overlap) {
					(void) fprintf(stderr,
					    "label error: EFI Labels do not "
					    "support overlapping partitions\n");
					}
					(void) fprintf(stderr,
					    "Partition %d overlaps partition "
					    "%d.\n", i, j);
					overlap = 1;
				}
			}
		}
	}
	/* make sure there is a reserved partition */
	if (resv_part == -1) {
		(void) fprintf(stderr,
		    "no reserved partition found\n");
	}
}
