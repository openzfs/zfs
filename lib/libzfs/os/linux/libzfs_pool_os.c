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
 * Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2018 by Delphix. All rights reserved.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>
 * Copyright (c) 2018 Datto Inc.
 * Copyright (c) 2017 Open-E, Inc. All Rights Reserved.
 * Copyright (c) 2017, Intel Corporation.
 * Copyright (c) 2018, loli10K <ezomori.nozomu@gmail.com>
 */

#include <errno.h>
#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <libgen.h>
#include <zone.h>
#include <sys/stat.h>
#include <sys/efi_partition.h>
#include <sys/systeminfo.h>
#include <sys/vtoc.h>
#include <sys/zfs_ioctl.h>
#include <sys/vdev_disk.h>
#include <dlfcn.h>
#include <libzutil.h>

#include "zfs_namecheck.h"
#include "zfs_prop.h"
#include "libzfs_impl.h"
#include "zfs_comutil.h"
#include "zfeature_common.h"

/*
 * If the device has being dynamically expanded then we need to relabel
 * the disk to use the new unallocated space.
 */
int
zpool_relabel_disk(libzfs_handle_t *hdl, const char *path, const char *msg)
{
	int fd, error;

	if ((fd = open(path, O_RDWR|O_DIRECT)) < 0) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "cannot "
		    "relabel '%s': unable to open device: %d"), path, errno);
		return (zfs_error(hdl, EZFS_OPENFAILED, msg));
	}

	/*
	 * It's possible that we might encounter an error if the device
	 * does not have any unallocated space left. If so, we simply
	 * ignore that error and continue on.
	 */
	error = efi_use_whole_disk(fd);

	/* Flush the buffers to disk and invalidate the page cache. */
	(void) fsync(fd);
	(void) ioctl(fd, BLKFLSBUF);

	(void) close(fd);
	if (error && error != VT_ENOSPC) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "cannot "
		    "relabel '%s': unable to read disk capacity"), path);
		return (zfs_error(hdl, EZFS_NOCAP, msg));
	}
	return (0);
}

/*
 * Read the EFI label from the config, if a label does not exist then
 * pass back the error to the caller. If the caller has passed a non-NULL
 * diskaddr argument then we set it to the starting address of the EFI
 * partition. If the caller has passed a non-NULL boolean argument, then
 * we set it to indicate if the disk does have efi system partition.
 */
static int
read_efi_label(nvlist_t *config, diskaddr_t *sb, boolean_t *system)
{
	char *path;
	int fd;
	char diskname[MAXPATHLEN];
	boolean_t boot = B_FALSE;
	int err = -1;
	int slice;

	if (nvlist_lookup_string(config, ZPOOL_CONFIG_PATH, &path) != 0)
		return (err);

	(void) snprintf(diskname, sizeof (diskname), "%s%s", DISK_ROOT,
	    strrchr(path, '/'));
	if ((fd = open(diskname, O_RDONLY|O_DIRECT)) >= 0) {
		struct dk_gpt *vtoc;

		if ((err = efi_alloc_and_read(fd, &vtoc)) >= 0) {
			for (slice = 0; slice < vtoc->efi_nparts; slice++) {
				if (vtoc->efi_parts[slice].p_tag == V_SYSTEM)
					boot = B_TRUE;
				if (vtoc->efi_parts[slice].p_tag == V_USR)
					break;
			}
			if (sb != NULL && vtoc->efi_parts[slice].p_tag == V_USR)
				*sb = vtoc->efi_parts[slice].p_start;
			if (system != NULL)
				*system = boot;
			efi_free(vtoc);
		}
		(void) close(fd);
	}
	return (err);
}

/*
 * determine where a partition starts on a disk in the current
 * configuration
 */
static diskaddr_t
find_start_block(nvlist_t *config)
{
	nvlist_t **child;
	uint_t c, children;
	diskaddr_t sb = MAXOFFSET_T;
	uint64_t wholedisk;

	if (nvlist_lookup_nvlist_array(config,
	    ZPOOL_CONFIG_CHILDREN, &child, &children) != 0) {
		if (nvlist_lookup_uint64(config,
		    ZPOOL_CONFIG_WHOLE_DISK,
		    &wholedisk) != 0 || !wholedisk) {
			return (MAXOFFSET_T);
		}
		if (read_efi_label(config, &sb, NULL) < 0)
			sb = MAXOFFSET_T;
		return (sb);
	}

	for (c = 0; c < children; c++) {
		sb = find_start_block(child[c]);
		if (sb != MAXOFFSET_T) {
			return (sb);
		}
	}
	return (MAXOFFSET_T);
}

static int
zpool_label_disk_check(char *path)
{
	struct dk_gpt *vtoc;
	int fd, err;

	if ((fd = open(path, O_RDONLY|O_DIRECT)) < 0)
		return (errno);

	if ((err = efi_alloc_and_read(fd, &vtoc)) != 0) {
		(void) close(fd);
		return (err);
	}

	if (vtoc->efi_flags & EFI_GPT_PRIMARY_CORRUPT) {
		efi_free(vtoc);
		(void) close(fd);
		return (EIDRM);
	}

	efi_free(vtoc);
	(void) close(fd);
	return (0);
}

/*
 * Generate a unique partition name for the ZFS member.  Partitions must
 * have unique names to ensure udev will be able to create symlinks under
 * /dev/disk/by-partlabel/ for all pool members.  The partition names are
 * of the form <pool>-<unique-id>.
 */
static void
zpool_label_name(char *label_name, int label_size, const char *str)
{
	uint64_t id = 0;
	int fd;

	fd = open("/dev/urandom", O_RDONLY);
	if (fd >= 0) {
		if (read(fd, &id, sizeof (id)) != sizeof (id))
			id = 0;

		close(fd);
	}

	if (id == 0)
		id = (((uint64_t)rand()) << 32) | (uint64_t)rand();

	snprintf(label_name, label_size, "%s-%016llx", str, (u_longlong_t)id);
}

/*
 * Label an individual disk.  The name provided is the short name,
 * stripped of any leading /dev path.
 */
int
zpool_label_disk(libzfs_handle_t *hdl, zpool_handle_t *zhp, const char *name,
    zpool_boot_label_t boot_type, uint64_t boot_size, int *slice)
{
	char path[MAXPATHLEN];
	struct dk_gpt *vtoc;
	int rval, fd;
	size_t resv = EFI_MIN_RESV_SIZE;
	uint64_t slice_size;
	diskaddr_t start_block;
	char errbuf[1024];

	/* prepare an error message just in case */
	(void) snprintf(errbuf, sizeof (errbuf),
	    dgettext(TEXT_DOMAIN, "cannot label '%s'"), name);

	if (zhp) {
		nvlist_t *nvroot;

		verify(nvlist_lookup_nvlist(zhp->zpool_config,
		    ZPOOL_CONFIG_VDEV_TREE, &nvroot) == 0);

		if (zhp->zpool_start_block == 0)
			start_block = find_start_block(nvroot);
		else
			start_block = zhp->zpool_start_block;
		zhp->zpool_start_block = start_block;
	} else {
		/* new pool */
		start_block = NEW_START_BLOCK;
	}

	(void) snprintf(path, sizeof (path), "%s/%s", DISK_ROOT, name);

	if ((fd = open(path, O_RDWR|O_DIRECT|O_EXCL)) < 0) {
		/*
		 * This shouldn't happen.  We've long since verified that this
		 * is a valid device.
		 */
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "cannot "
		    "label '%s': unable to open device: %d"), path, errno);
		return (zfs_error(hdl, EZFS_OPENFAILED, errbuf));
	}

	if (efi_alloc_and_init(fd, EFI_NUMPAR, &vtoc) != 0) {
		/*
		 * The only way this can fail is if we run out of memory, or we
		 * were unable to read the disk's capacity
		 */
		if (errno == ENOMEM)
			(void) no_memory(hdl);

		(void) close(fd);
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "cannot "
		    "label '%s': unable to read disk capacity"), path);

		return (zfs_error(hdl, EZFS_NOCAP, errbuf));
	}

	/*
	 * Why we use V_USR: V_BACKUP confuses users, and is considered
	 * disposable by some EFI utilities (since EFI doesn't have a backup
	 * slice).  V_UNASSIGNED is supposed to be used only for zero size
	 * partitions, and efi_write() will fail if we use it.  V_ROOT, V_BOOT,
	 * etc. were all pretty specific.  V_USR is as close to reality as we
	 * can get, in the absence of V_OTHER.
	 */
	/* first fix the partition start block */
	if (start_block == MAXOFFSET_T)
		start_block = NEW_START_BLOCK;

	/*
	 * EFI System partition is using slice 0.
	 * ZFS is on slice 1 and slice 8 is reserved.
	 * We assume the GPT partition table without system
	 * partition has zfs p_start == NEW_START_BLOCK.
	 * If start_block != NEW_START_BLOCK, it means we have
	 * system partition. Correct solution would be to query/cache vtoc
	 * from existing vdev member.
	 */
	if (boot_type == ZPOOL_CREATE_BOOT_LABEL) {
		if (boot_size % vtoc->efi_lbasize != 0) {
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "boot partition size must be a multiple of %d"),
			    vtoc->efi_lbasize);
			(void) close(fd);
			efi_free(vtoc);
			return (zfs_error(hdl, EZFS_LABELFAILED, errbuf));
		}
		/*
		 * System partition size checks.
		 * Note the 1MB is quite arbitrary value, since we
		 * are creating dedicated pool, it should be enough
		 * to hold fat + efi bootloader. May need to be
		 * adjusted if the bootloader size will grow.
		 */
		if (boot_size < 1024 * 1024) {
			char buf[64];
			zfs_nicenum(boot_size, buf, sizeof (buf));
			zfs_error_aux(hdl, dgettext(TEXT_DOMAIN,
			    "Specified size %s for EFI System partition is too "
			    "small, the minimum size is 1MB."), buf);
			(void) close(fd);
			efi_free(vtoc);
			return (zfs_error(hdl, EZFS_LABELFAILED, errbuf));
		}
		/* 33MB is tested with mkfs -F pcfs */
		if (hdl->libzfs_printerr &&
		    ((vtoc->efi_lbasize == 512 &&
		    boot_size < 33 * 1024 * 1024) ||
		    (vtoc->efi_lbasize == 4096 &&
		    boot_size < 256 * 1024 * 1024)))  {
			char buf[64];
			zfs_nicenum(boot_size, buf, sizeof (buf));
			(void) fprintf(stderr, dgettext(TEXT_DOMAIN,
			    "Warning: EFI System partition size %s is "
			    "not allowing to create FAT32 file\nsystem, which "
			    "may result in unbootable system.\n"), buf);
		}
		/* Adjust zfs partition start by size of system partition. */
		start_block += boot_size / vtoc->efi_lbasize;
	}

	if (start_block == NEW_START_BLOCK) {
		/*
		 * Use default layout.
		 * ZFS is on slice 0 and slice 8 is reserved.
		 */
		slice_size = vtoc->efi_last_u_lba + 1;
		slice_size -= EFI_MIN_RESV_SIZE;
		slice_size -= start_block;
		slice_size = P2ALIGN(slice_size, PARTITION_END_ALIGNMENT);
		if (slice != NULL)
			*slice = 0;

		vtoc->efi_parts[0].p_start = start_block;
		vtoc->efi_parts[0].p_size = slice_size;

		vtoc->efi_parts[0].p_tag = V_USR;
		zpool_label_name(vtoc->efi_parts[0].p_name,
		    EFI_PART_NAME_LEN, "zfs");

		vtoc->efi_parts[8].p_start = slice_size + start_block;
		vtoc->efi_parts[8].p_size = resv;
		vtoc->efi_parts[8].p_tag = V_RESERVED;
	} else {
		slice_size = start_block - NEW_START_BLOCK;
		vtoc->efi_parts[0].p_start = NEW_START_BLOCK;
		vtoc->efi_parts[0].p_size = slice_size;
		vtoc->efi_parts[0].p_tag = V_SYSTEM;
		zpool_label_name(vtoc->efi_parts[0].p_name,
		    EFI_PART_NAME_LEN, "loader");
		if (slice != NULL)
			*slice = 1;
		/* prepare slice 1 */
		slice_size = vtoc->efi_last_u_lba + 1 - slice_size;
		slice_size -= resv;
		slice_size -= NEW_START_BLOCK;
		slice_size = P2ALIGN(slice_size, PARTITION_END_ALIGNMENT);
		vtoc->efi_parts[1].p_start = start_block;
		vtoc->efi_parts[1].p_size = slice_size;
		vtoc->efi_parts[1].p_tag = V_USR;
		zpool_label_name(vtoc->efi_parts[1].p_name,
		    EFI_PART_NAME_LEN, "zfs");

		vtoc->efi_parts[8].p_start = slice_size + start_block;
		vtoc->efi_parts[8].p_size = resv;
		vtoc->efi_parts[8].p_tag = V_RESERVED;
	}

	rval = efi_write(fd, vtoc);

	/* Flush the buffers to disk and invalidate the page cache. */
	(void) fsync(fd);
	(void) ioctl(fd, BLKFLSBUF);

	if (rval == 0)
		rval = efi_rescan(fd);

	/*
	 * Some block drivers (like pcata) may not support EFI GPT labels.
	 * Print out a helpful error message directing the user to manually
	 * label the disk and give a specific slice.
	 */
	if (rval != 0) {
		(void) close(fd);
		efi_free(vtoc);

		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "try using "
		    "parted(8) and then provide a specific slice: %d"), rval);
		return (zfs_error(hdl, EZFS_LABELFAILED, errbuf));
	}

	(void) close(fd);
	efi_free(vtoc);

	(void) snprintf(path, sizeof (path), "%s/%s", DISK_ROOT, name);
	(void) zfs_append_partition(path, MAXPATHLEN);

	/* Wait to udev to signal use the device has settled. */
	rval = zpool_label_disk_wait(path, DISK_LABEL_WAIT);
	if (rval) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "failed to "
		    "detect device partitions on '%s': %d"), path, rval);
		return (zfs_error(hdl, EZFS_LABELFAILED, errbuf));
	}

	/* We can't be to paranoid.  Read the label back and verify it. */
	(void) snprintf(path, sizeof (path), "%s/%s", DISK_ROOT, name);
	rval = zpool_label_disk_check(path);
	if (rval) {
		zfs_error_aux(hdl, dgettext(TEXT_DOMAIN, "freshly written "
		    "EFI label on '%s' is damaged.  Ensure\nthis device "
		    "is not in use, and is functioning properly: %d"),
		    path, rval);
		return (zfs_error(hdl, EZFS_LABELFAILED, errbuf));
	}
	return (0);
}
