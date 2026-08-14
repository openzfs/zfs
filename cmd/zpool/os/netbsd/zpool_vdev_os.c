// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2016, 2017 Intel Corporation.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <libintl.h>
#include <libnvpair.h>
#include <libzutil.h>
#include <limits.h>
#include <sys/spa.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <paths.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/mount.h>
#include <sys/swap.h>
#include <sys/mntent.h>
#include <sys/disklabel.h>
#include <sys/dkio.h>
#include <sys/ioctl.h>

#include "zpool_util.h"
#include <sys/zfs_context.h>

static const char *
after_dev(const char *path)
{
	if (strncmp(path, _PATH_DEV, sizeof (_PATH_DEV) - 1) == 0)
		return (path + sizeof (_PATH_DEV) - 1);
	return (path);
}

static const char *
disk_leaf(const char *path)
{
	const char *p = after_dev(path);
	const char *base = strrchr(p, '/');

	if (base != NULL)
		p = base + 1;
	if (p[0] == 'r' && p[1] != '\0' && zfs_dev_is_disk_class(p + 1))
		return (p + 1);
	return (p);
}

static boolean_t
dev_raw_twin(const char *path, char *out, size_t n)
{
	const char *p = after_dev(path);
	const char *base = strrchr(p, '/') != NULL ? strrchr(p, '/') + 1 : p;
	const char *leaf = disk_leaf(path);

	if (base[0] == 'r' && zfs_dev_is_disk_class(base + 1))
		return (snprintf(out, n, "%s%s", _PATH_DEV, leaf) < (int)n);
	if (zfs_dev_is_disk_class(leaf))
		return (snprintf(out, n, "%sr%s", _PATH_DEV, leaf) < (int)n);
	return (B_FALSE);
}

static boolean_t
dev_idents_match(const struct stat *sa, const struct stat *sb)
{
	if (sa->st_dev == sb->st_dev && sa->st_ino == sb->st_ino)
		return (B_TRUE);
	if (!S_ISCHR(sa->st_mode) && !S_ISBLK(sa->st_mode))
		return (B_FALSE);
	if (!S_ISCHR(sb->st_mode) && !S_ISBLK(sb->st_mode))
		return (B_FALSE);
	if (sa->st_rdev == sb->st_rdev)
		return (B_TRUE);
	/*
	 * Do not pair by minor alone, and do not require matching
	 * majors either.  On NetBSD /dev/wd1a (block) and /dev/rwd1a
	 * (char) share a minor and have distinct majors; unrelated
	 * drivers also reuse minor 0.  Identity is inode or rdev.
	 * The raw/block twin is stat()ed separately in
	 * dev_fill_stats() so wd1a vs rwd1a still matches.
	 */
	return (B_FALSE);
}

static int
dev_fill_stats(const char *path, struct stat st[2])
{
	char twin[PATH_MAX];
	int n = 0;

	if (stat(path, &st[n]) == 0)
		n++;
	if (dev_raw_twin(path, twin, sizeof (twin)) &&
	    stat(twin, &st[n]) == 0)
		n++;
	return (n);
}

static boolean_t
dev_names_match(const char *a, const char *b)
{
	char ra[PATH_MAX], rb[PATH_MAX];
	struct stat sa[2], sb[2];
	const char *pa = a, *pb = b;
	int na, nb, i, j;

	if (a == NULL || b == NULL || a[0] == '\0' || b[0] == '\0')
		return (B_FALSE);
	if (realpath(a, ra) != NULL)
		pa = ra;
	if (realpath(b, rb) != NULL)
		pb = rb;
	if (strcmp(pa, pb) == 0)
		return (B_TRUE);

	na = dev_fill_stats(pa, sa);
	nb = dev_fill_stats(pb, sb);
	for (i = 0; i < na; i++) {
		for (j = 0; j < nb; j++) {
			if (dev_idents_match(&sa[i], &sb[j]))
				return (B_TRUE);
		}
	}
	/*
	 * Name fallback only when we could not stat either side.
	 * Same leaf after a failed identity match is a different
	 * device (wd0a vs sd0a both "look" like disks).
	 */
	if (na == 0 || nb == 0)
		return (strcmp(disk_leaf(pa), disk_leaf(pb)) == 0);
	return (B_FALSE);
}

#ifndef MAXPARTITIONS
#define	MAXPARTITIONS	16
#endif
#ifndef DISKUNIT
#define	DISKUNIT(dev)	(minor(dev) / MAXPARTITIONS)
#endif

static boolean_t
leaf_named_class(const char *leaf, const char *cls)
{
	size_t n = strlen(cls);

	return (strncmp(leaf, cls, n) == 0 &&
	    (leaf[n] == '\0' || (leaf[n] >= '0' && leaf[n] <= '9')));
}

static boolean_t
dev_disklabel_class(const char *path)
{
	const char *leaf = disk_leaf(path);

	/*
	 * Drivers that encode unit = minor/MAXPARTITIONS, so wd1a vs
	 * wd1c (and vnd0a vs vnd0c, ...) are the same disk.  dk
	 * wedges are one minor per volume -- DISKUNIT would collapse
	 * dk0 and dk1.  md/fd/cd are not ZFS vdev targets.
	 */
	if (!zfs_dev_is_disk_class(leaf))
		return (B_FALSE);
	if (leaf_named_class(leaf, "dk") || leaf_named_class(leaf, "md") ||
	    leaf_named_class(leaf, "fd") || leaf_named_class(leaf, "cd"))
		return (B_FALSE);
	return (B_TRUE);
}

static boolean_t
dev_same_diskunit(const struct stat *sa, const struct stat *sb)
{
	if (!S_ISCHR(sa->st_mode) && !S_ISBLK(sa->st_mode))
		return (B_FALSE);
	if (!S_ISCHR(sb->st_mode) && !S_ISBLK(sb->st_mode))
		return (B_FALSE);
	if (sa->st_rdev == sb->st_rdev)
		return (B_TRUE);
	if (major(sa->st_rdev) != major(sb->st_rdev))
		return (B_FALSE);
	return (DISKUNIT(sa->st_rdev) == DISKUNIT(sb->st_rdev));
}

/*
 * True if `used` is the same device as `cand`, or a partition of the
 * same disklabel disk (wd1c vs wd1a).  Identity is rdev / inode, not
 * a leaf-name prefix: a file named wd1 must not collide with wd1a.
 */
static boolean_t
dev_overlaps(const char *cand, const char *used)
{
	struct stat sa[2], sb[2];
	int na, nb, i, j;

	if (dev_names_match(cand, used))
		return (B_TRUE);
	na = dev_fill_stats(cand, sa);
	nb = dev_fill_stats(used, sb);
	if (na == 0 || nb == 0)
		return (B_FALSE);
	if (!dev_disklabel_class(cand) || !dev_disklabel_class(used))
		return (B_FALSE);
	for (i = 0; i < na; i++) {
		for (j = 0; j < nb; j++) {
			if (dev_same_diskunit(&sa[i], &sb[j]))
				return (B_TRUE);
		}
	}
	return (B_FALSE);
}

/*
 * Mounted FFS/MSDOS/etc and active swap cannot be given to zpool
 * create, even with -f.  A live mount or swap consumer is not a
 * "maybe in use" hint.  -f is still honoured by check_file_generic
 * for exported-pool / in-use-file cases.
 */
static int
check_netbsd_inuse(const char *path, boolean_t force)
{
	struct statvfs *fs = NULL;
	struct swapent *se = NULL;
	int n, nalloc = 0, i, tries, ret = 0, fd;

	(void) force;

	/*
	 * O_EXCL: a disk held by another subsystem (RAIDframe,
	 * cgd, ccd) returns EBUSY on NetBSD when the driver
	 * honours exclusive open.  Mount and swap are checked
	 * below even when O_EXCL is a no-op.
	 */
	fd = open(path, O_RDWR | O_EXCL | O_CLOEXEC | O_NONBLOCK);
	if (fd < 0) {
		if (errno == EBUSY) {
			vdev_error(gettext("%s is in use\n"), path);
			return (-1);
		}
	} else {
		(void) close(fd);
	}
	{
		char twin[PATH_MAX];
		int tfd;

		if (dev_raw_twin(path, twin, sizeof (twin))) {
			tfd = open(twin, O_RDWR | O_EXCL | O_CLOEXEC |
			    O_NONBLOCK);
			if (tfd < 0) {
				if (errno == EBUSY) {
					vdev_error(gettext("%s is in use\n"),
					    twin);
					return (-1);
				}
			} else {
				(void) close(tfd);
			}
		}
	}

	n = getvfsstat(NULL, 0, ST_NOWAIT);
	if (n < 0) {
		vdev_error(gettext("cannot read mount table: %s\n"),
		    strerror(errno));
		return (-1);
	}
	for (tries = 0; n > 0 && tries < 8; tries++) {
		int got;

		nalloc = n + 8;
		free(fs);
		fs = calloc((size_t)nalloc, sizeof (*fs));
		if (fs == NULL) {
			vdev_error(gettext("cannot allocate mount table\n"));
			return (-1);
		}
		got = getvfsstat(fs, (size_t)nalloc * sizeof (*fs), ST_NOWAIT);
		if (got < 0) {
			vdev_error(gettext("cannot read mount table: %s\n"),
			    strerror(errno));
			free(fs);
			return (-1);
		}
		if (got < nalloc) {
			n = got;
			break;
		}
		n = nalloc + 8;
	}
	if (n > nalloc)
		n = nalloc;
	for (i = 0; i < n; i++) {
		if (!dev_overlaps(path, fs[i].f_mntfromname))
			continue;
		vdev_error(gettext("%s is mounted on %s\n"),
		    path, fs[i].f_mntonname);
		ret = -1;
		break;
	}
	free(fs);
	if (ret != 0)
		return (ret);

	n = swapctl(SWAP_NSWAP, NULL, 0);
	if (n < 0) {
		vdev_error(gettext("cannot read swap table: %s\n"),
		    strerror(errno));
		return (-1);
	}
	for (tries = 0; n > 0 && tries < 8; tries++) {
		int got;

		nalloc = n + 4;
		free(se);
		se = calloc((size_t)nalloc, sizeof (*se));
		if (se == NULL) {
			vdev_error(gettext("cannot allocate swap table\n"));
			return (-1);
		}
		got = swapctl(SWAP_STATS, se, nalloc);
		if (got < 0) {
			vdev_error(gettext("cannot read swap table: %s\n"),
			    strerror(errno));
			free(se);
			return (-1);
		}
		if (got < nalloc) {
			n = got;
			break;
		}
		n = nalloc + 4;
	}
	if (n > nalloc)
		n = nalloc;
	for (i = 0; i < n; i++) {
		if ((se[i].se_flags & SWF_ENABLE) == 0)
			continue;
		if (!dev_overlaps(path, se[i].se_path))
			continue;
		vdev_error(gettext("%s is in use as swap\n"), path);
		ret = -1;
		break;
	}
	free(se);
	return (ret);
}

/*
 * UFS superblock locations and fs_magic / fs_bsize offsets from
 * NetBSD <ufs/ffs/fs.h>.  Hardcoded so zpool does not include the
 * kernel UFS headers (they are not userspace-clean).
 *
 * zpool_create_002_pos newfs's a disk and expects create without -f
 * to fail.  FreeBSD only notices a mounted filesystem; NetBSD's
 * test does not mount it.  Mounted FFS is already rejected above.
 * This catches an unmounted UFS1/UFS2 superblock.  -f skips it.
 */
#define	NB_SBLOCK_UFS1			8192
#define	NB_SBLOCK_UFS2			65536
#define	NB_SBLOCK_PIGGY			262144
#define	NB_FS_BSIZE_OFF			48
#define	NB_FS_MAGIC_OFF			1372
#define	NB_FS_UFS1_MAGIC		0x011954
#define	NB_FS_UFS2_MAGIC		0x19540119
#define	NB_FS_UFS2EA_MAGIC		0x19012038
#define	NB_FS_UFS1_MAGIC_SWAPPED	0x54190100
#define	NB_FS_UFS2_MAGIC_SWAPPED	0x19015419
#define	NB_FS_UFS2EA_MAGIC_SWAPPED	0x38200119

static uint32_t
nb_bswap32(uint32_t v)
{
	return ((v >> 24) | ((v >> 8) & 0xff00) |
	    ((v << 8) & 0xff0000) | (v << 24));
}

static boolean_t
nb_ufs_bsize_ok(uint32_t b)
{
	return (b >= 4096 && b <= 65536 && (b & (b - 1)) == 0);
}

static boolean_t
nb_ufs_read_u32(int fd, off_t off, uint32_t *out)
{
	uint8_t sec[512];
	off_t base = off & ~(off_t)(sizeof (sec) - 1);
	size_t disp = (size_t)(off - base);

	/*
	 * Raw disks reject a short unaligned pread (and O_NONBLOCK).
	 * Read the whole sector the field lives in.
	 */
	if (pread(fd, sec, sizeof (sec), base) != (ssize_t)sizeof (sec))
		return (B_FALSE);
	if (disp > sizeof (sec) - sizeof (*out))
		return (B_FALSE);
	(void) memcpy(out, sec + disp, sizeof (*out));
	return (B_TRUE);
}

static boolean_t
nb_ufs_at(int fd, off_t base)
{
	static const off_t sblocks[] = {
		NB_SBLOCK_UFS2, NB_SBLOCK_UFS1, NB_SBLOCK_PIGGY
	};
	int i;

	for (i = 0; i < (int)(sizeof (sblocks) / sizeof (sblocks[0])); i++) {
		off_t sb = base + sblocks[i];
		uint32_t magic, bsize;
		boolean_t swapped = B_FALSE;

		if (!nb_ufs_read_u32(fd, sb + NB_FS_MAGIC_OFF, &magic))
			continue;
		if (magic == NB_FS_UFS1_MAGIC_SWAPPED ||
		    magic == NB_FS_UFS2_MAGIC_SWAPPED ||
		    magic == NB_FS_UFS2EA_MAGIC_SWAPPED)
			swapped = B_TRUE;
		else if (magic != NB_FS_UFS1_MAGIC &&
		    magic != NB_FS_UFS2_MAGIC &&
		    magic != NB_FS_UFS2EA_MAGIC)
			continue;
		if (!nb_ufs_read_u32(fd, sb + NB_FS_BSIZE_OFF, &bsize))
			continue;
		if (swapped)
			bsize = nb_bswap32(bsize);
		if (nb_ufs_bsize_ok(bsize))
			return (B_TRUE);
	}
	return (B_FALSE);
}

static int
check_netbsd_ufs(const char *path)
{
	struct disklabel lab;
	struct stat st;
	int fd, i, n;
	boolean_t raw;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return (0);

	if (nb_ufs_at(fd, 0)) {
		vdev_error(gettext("%s contains a filesystem of "
		    "type 'ufs'\n"), path);
		(void) close(fd);
		return (-1);
	}

	/*
	 * /dev/rldN and /dev/rldNd are the raw whole-disk partition.
	 * A UFS living on a later partition is not at offset 0 of
	 * that device.  A partition device (ldNa) is already relative
	 * to its start, so do not add p_offset again.
	 */
	raw = B_FALSE;
	if (fstat(fd, &st) == 0 &&
	    (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) &&
	    (minor(st.st_rdev) % MAXPARTITIONS) == RAW_PART)
		raw = B_TRUE;
	if (raw && ioctl(fd, DIOCGDINFO, &lab) == 0) {
		n = lab.d_npartitions;
		if (n > MAXPARTITIONS)
			n = MAXPARTITIONS;
		for (i = 0; i < n; i++) {
			off_t base;
			unsigned sec;

			if (i == RAW_PART || lab.d_partitions[i].p_size == 0)
				continue;
			sec = lab.d_secsize != 0 ? lab.d_secsize : 512;
			base = (off_t)lab.d_partitions[i].p_offset * sec;
			if (base == 0)
				continue;
			if (nb_ufs_at(fd, base)) {
				vdev_error(gettext("%s contains a filesystem "
				    "of type 'ufs'\n"), path);
				(void) close(fd);
				return (-1);
			}
		}
	}
	(void) close(fd);
	return (0);
}

int
check_device(const char *name, boolean_t force, boolean_t isspare,
    boolean_t iswholedisk)
{
	(void) iswholedisk;
	char path[MAXPATHLEN];

	if (name[0] != '/') {
		if (snprintf(path, sizeof (path), "%s%s", _PATH_DEV, name) >=
		    (int)sizeof (path)) {
			vdev_error(gettext("device name too long: %s\n"), name);
			return (-1);
		}
	} else {
		if (strlcpy(path, name, sizeof (path)) >= sizeof (path)) {
			vdev_error(gettext("device name too long: %s\n"), name);
			return (-1);
		}
	}

	if (check_netbsd_inuse(path, force) != 0)
		return (-1);
	/* Unmounted UFS is overridable with -f; a live mount is not. */
	if (!force && check_netbsd_ufs(path) != 0)
		return (-1);
	return (check_file(path, force, isspare));
}

boolean_t
check_sector_size_database(char *path, int *sector_size)
{
	(void) path, (void) sector_size;
	return (0);
}

void
after_zpool_upgrade(zpool_handle_t *zhp)
{
	char bootfs[ZPOOL_MAXPROPLEN];

	if (zpool_get_prop(zhp, ZPOOL_PROP_BOOTFS, bootfs,
	    sizeof (bootfs), NULL, B_FALSE) == 0 &&
	    strcmp(bootfs, "-") != 0) {
		(void) printf(gettext("Pool '%s' has the bootfs "
		    "property set, you might need to update\nthe boot "
		    "property set; update the NetBSD boot blocks if "
		    "this pool is used to boot.\n"), zpool_get_name(zhp));
	}
}

int
check_file(const char *file, boolean_t force, boolean_t isspare)
{
	return (check_file_generic(file, force, isspare));
}

int
zpool_power_current_state(zpool_handle_t *zhp, char *vdev)
{

	(void) zhp;
	(void) vdev;
	/* Enclosure slot power not supported on NetBSD yet */
	return (-1);
}

int
zpool_power(zpool_handle_t *zhp, char *vdev, boolean_t turn_on)
{

	(void) zhp;
	(void) vdev;
	(void) turn_on;
	/* Enclosure slot power not supported on NetBSD yet */
	return (ENOTSUP);
}
