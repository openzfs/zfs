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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Functions to convert between a list of vdevs and an nvlist representing the
 * configuration.  Each entry in the list can be one of:
 *
 * 	Device vdevs
 * 		disk=(path=..., devid=...)
 * 		file=(path=...)
 *
 * 	Group vdevs
 * 		raidz[1|2]=(...)
 * 		mirror=(...)
 *
 * 	Hot spares
 *
 * While the underlying implementation supports it, group vdevs cannot contain
 * other group vdevs.  All userland verification of devices is contained within
 * this file.  If successful, the nvlist returned can be passed directly to the
 * kernel; we've done as much verification as possible in userland.
 *
 * Hot spares are a special case, and passed down as an array of disk vdevs, at
 * the same level as the root of the vdev tree.
 *
 * The only function exported by this file is 'make_root_vdev'.  The
 * function performs several passes:
 *
 * 	1. Construct the vdev specification.  Performs syntax validation and
 *         makes sure each device is valid.
 * 	2. Check for devices in use.  Using libblkid to make sure that no
 *         devices are also in use.  Some can be overridden using the 'force'
 *         flag, others cannot.
 * 	3. Check for replication errors if the 'force' flag is not specified.
 *         validates that the replication level is consistent across the
 *         entire pool.
 * 	4. Call libzfs to label any whole disks with an EFI label.
 */

#include <assert.h>
#include <ctype.h>
#include <devid.h>
#include <errno.h>
#include <fcntl.h>
#include <libintl.h>
#include <libnvpair.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/efi_partition.h>
#include <sys/stat.h>
#include <sys/vtoc.h>
#include <sys/mntent.h>
#include <uuid/uuid.h>
#ifdef HAVE_LIBBLKID
#include <blkid/blkid.h>
#else
#define blkid_cache void *
#endif /* HAVE_LIBBLKID */

#include "zpool_util.h"

/*
 * For any given vdev specification, we can have multiple errors.  The
 * vdev_error() function keeps track of whether we have seen an error yet, and
 * prints out a header if its the first error we've seen.
 */
boolean_t error_seen;
boolean_t is_force;

/*PRINTFLIKE1*/
static void
vdev_error(const char *fmt, ...)
{
	va_list ap;

	if (!error_seen) {
		(void) fprintf(stderr, gettext("invalid vdev specification\n"));
		if (!is_force)
			(void) fprintf(stderr, gettext("use '-f' to override "
			    "the following errors:\n"));
		else
			(void) fprintf(stderr, gettext("the following errors "
			    "must be manually repaired:\n"));
		error_seen = B_TRUE;
	}

	va_start(ap, fmt);
	(void) vfprintf(stderr, fmt, ap);
	va_end(ap);
}

/*
 * Check that a file is valid.  All we can do in this case is check that it's
 * not in use by another pool, and not in use by swap.
 */
static int
check_file(const char *file, boolean_t force, boolean_t isspare)
{
	char  *name;
	int fd;
	int ret = 0;
	pool_state_t state;
	boolean_t inuse;

	if ((fd = open(file, O_RDONLY)) < 0)
		return (0);

	if (zpool_in_use(g_zfs, fd, &state, &name, &inuse) == 0 && inuse) {
		const char *desc;

		switch (state) {
		case POOL_STATE_ACTIVE:
			desc = gettext("active");
			break;

		case POOL_STATE_EXPORTED:
			desc = gettext("exported");
			break;

		case POOL_STATE_POTENTIALLY_ACTIVE:
			desc = gettext("potentially active");
			break;

		default:
			desc = gettext("unknown");
			break;
		}

		/*
		 * Allow hot spares to be shared between pools.
		 */
		if (state == POOL_STATE_SPARE && isspare)
			return (0);

		if (state == POOL_STATE_ACTIVE ||
		    state == POOL_STATE_SPARE || !force) {
			switch (state) {
			case POOL_STATE_SPARE:
				vdev_error(gettext("%s is reserved as a hot "
				    "spare for pool %s\n"), file, name);
				break;
			default:
				vdev_error(gettext("%s is part of %s pool "
				    "'%s'\n"), file, desc, name);
				break;
			}
			ret = -1;
		}

		free(name);
	}

	(void) close(fd);
	return (ret);
}

static void
check_error(int err)
{
	(void) fprintf(stderr, gettext("warning: device in use checking "
	    "failed: %s\n"), strerror(err));
}

static int
check_slice(const char *path, blkid_cache cache, int force, boolean_t isspare)
{
	int err;
#ifdef HAVE_LIBBLKID
	char *value;

	/* No valid type detected device is safe to use */
	value = blkid_get_tag_value(cache, "TYPE", path);
	if (value == NULL)
		return (0);

	/*
	 * If libblkid detects a ZFS device, we check the device
	 * using check_file() to see if it's safe.  The one safe
	 * case is a spare device shared between multiple pools.
	 */
	if (strcmp(value, "zfs") == 0) {
		err = check_file(path, force, isspare);
	} else {
		if (force) {
			err = 0;
		} else {
			err = -1;
			vdev_error(gettext("%s contains a filesystem of "
				   "type '%s'\n"), path, value);
		}
	}

	free(value);
#else
	err = check_file(path, force, isspare);
#endif /* HAVE_LIBBLKID */

	return (err);
}

/*
 * Validate a whole disk.  Iterate over all slices on the disk and make sure
 * that none is in use by calling check_slice().
 */
static int
check_disk(const char *path, blkid_cache cache, int force,
	   boolean_t isspare, boolean_t iswholedisk)
{
	struct dk_gpt *vtoc;
	char slice_path[MAXPATHLEN];
	int err = 0;
	int fd, i;

	/* This is not a wholedisk we only check the given partition */
	if (!iswholedisk)
		return check_slice(path, cache, force, isspare);

	/*
	 * When the device is a whole disk try to read the efi partition
	 * label.  If this is successful we safely check the all of the
	 * partitions.  However, when it fails it may simply be because
	 * the disk is partitioned via the MBR.  Since we currently can
	 * not easily decode the MBR return a failure and prompt to the
	 * user to use force option since we cannot check the partitions.
	 */
	if ((fd = open(path, O_RDWR|O_DIRECT|O_EXCL)) < 0) {
		check_error(errno);
		return -1;
	}

	if ((err = efi_alloc_and_read(fd, &vtoc)) != 0) {
		(void) close(fd);

		if (force) {
			return 0;
		} else {
			vdev_error(gettext("%s does not contain an EFI "
			    "label but it may contain partition\n"
			    "information in the MBR.\n"), path);
			return -1;
		}
	}

	/*
	 * The primary efi partition label is damaged however the secondary
	 * label at the end of the device is intact.  Rather than use this
	 * label we should play it safe and treat this as a non efi device.
	 */
	if (vtoc->efi_flags & EFI_GPT_PRIMARY_CORRUPT) {
		efi_free(vtoc);
		(void) close(fd);

		if (force) {
			/* Partitions will no be created using the backup */
			return 0;
		} else {
			vdev_error(gettext("%s contains a corrupt primary "
			    "EFI label.\n"), path);
			return -1;
		}
	}

	for (i = 0; i < vtoc->efi_nparts; i++) {

		if (vtoc->efi_parts[i].p_tag == V_UNASSIGNED ||
		    uuid_is_null((uchar_t *)&vtoc->efi_parts[i].p_guid))
			continue;

		if (strncmp(path, UDISK_ROOT, strlen(UDISK_ROOT)) == 0)
			(void) snprintf(slice_path, sizeof (slice_path),
			    "%s%s%d", path, "-part", i+1);
		else
			(void) snprintf(slice_path, sizeof (slice_path),
			    "%s%s%d", path, isdigit(path[strlen(path)-1]) ?
			    "p" : "", i+1);

		err = check_slice(slice_path, cache, force, isspare);
		if (err)
			break;
	}

	efi_free(vtoc);
	(void) close(fd);

        return (err);
}

static int
check_device(const char *path, boolean_t force,
	     boolean_t isspare, boolean_t iswholedisk)
{
	static blkid_cache cache = NULL;

#ifdef HAVE_LIBBLKID
	/*
	 * There is no easy way to add a correct blkid_put_cache() call,
	 * memory will be reclaimed when the command exits.
	 */
	if (cache == NULL) {
		int err;

		if ((err = blkid_get_cache(&cache, NULL)) != 0) {
			check_error(err);
			return -1;
		}

		if ((err = blkid_probe_all(cache)) != 0) {
			blkid_put_cache(cache);
			check_error(err);
			return -1;
		}
	}
#endif /* HAVE_LIBBLKID */

	return check_disk(path, cache, force, isspare, iswholedisk);
}

/*
 * By "whole disk" we mean an entire physical disk (something we can
 * label, toggle the write cache on, etc.) as opposed to the full
 * capacity of a pseudo-device such as lofi or did.  We act as if we
 * are labeling the disk, which should be a pretty good test of whether
 * it's a viable device or not.  Returns B_TRUE if it is and B_FALSE if
 * it isn't.
 */
static boolean_t
is_whole_disk(const char *path)
{
	struct dk_gpt *label;
	int	fd;

	if ((fd = open(path, O_RDWR|O_DIRECT|O_EXCL)) < 0)
		return (B_FALSE);
	if (efi_alloc_and_init(fd, EFI_NUMPAR, &label) != 0) {
		(void) close(fd);
		return (B_FALSE);
	}
	efi_free(label);
	(void) close(fd);
	return (B_TRUE);
}

/*
 * This may be a shorthand device path or it could be total gibberish.
 * Check to see if it is a known device available in zfs_vdev_paths.
 * As part of this check, see if we've been given an entire disk
 * (minus the slice number).
 */
static int
is_shorthand_path(const char *arg, char *path,
                  struct stat64 *statbuf, boolean_t *wholedisk)
{
	int error;

	error = zfs_resolve_shortname(arg, path, MAXPATHLEN);
	if (error == 0) {
		*wholedisk = is_whole_disk(path);
		if (*wholedisk || (stat64(path, statbuf) == 0))
			return (0);
	}

	strlcpy(path, arg, sizeof(path));
	memset(statbuf, 0, sizeof(*statbuf));
	*wholedisk = B_FALSE;

	return (error);
}

/*
 * Create a leaf vdev.  Determine if this is a file or a device.  If it's a
 * device, fill in the device id to make a complete nvlist.  Valid forms for a
 * leaf vdev are:
 *
 *	/dev/xxx	Complete disk path
 *	/xxx		Full path to file
 *	xxx		Shorthand for <zfs_vdev_paths>/xxx
 */
static nvlist_t *
make_leaf_vdev(nvlist_t *props, const char *arg, uint64_t is_log)
{
	char path[MAXPATHLEN];
	struct stat64 statbuf;
	nvlist_t *vdev = NULL;
	char *type = NULL;
	boolean_t wholedisk = B_FALSE;
	int err;

	/*
	 * Determine what type of vdev this is, and put the full path into
	 * 'path'.  We detect whether this is a device of file afterwards by
	 * checking the st_mode of the file.
	 */
	if (arg[0] == '/') {
		/*
		 * Complete device or file path.  Exact type is determined by
		 * examining the file descriptor afterwards.  Symbolic links
		 * are resolved to their real paths for the is_whole_disk()
		 * and S_ISBLK/S_ISREG type checks.  However, we are careful
		 * to store the given path as ZPOOL_CONFIG_PATH to ensure we
		 * can leverage udev's persistent device labels.
		 */
		if (realpath(arg, path) == NULL) {
			(void) fprintf(stderr,
			    gettext("cannot resolve path '%s'\n"), arg);
			return (NULL);
		}

		wholedisk = is_whole_disk(path);
		if (!wholedisk && (stat64(path, &statbuf) != 0)) {
			(void) fprintf(stderr,
			    gettext("cannot open '%s': %s\n"),
			    path, strerror(errno));
			return (NULL);
		}

		/* After is_whole_disk() check restore original passed path */
		strlcpy(path, arg, MAXPATHLEN);
	} else {
		err = is_shorthand_path(arg, path, &statbuf, &wholedisk);
		if (err != 0) {
			/*
			 * If we got ENOENT, then the user gave us
			 * gibberish, so try to direct them with a
			 * reasonable error message.  Otherwise,
			 * regurgitate strerror() since it's the best we
			 * can do.
			 */
			if (err == ENOENT) {
				(void) fprintf(stderr,
				    gettext("cannot open '%s': no such "
				    "device in %s\n"), arg, DISK_ROOT);
				(void) fprintf(stderr,
				    gettext("must be a full path or "
				    "shorthand device name\n"));
				return (NULL);
			} else {
				(void) fprintf(stderr,
				    gettext("cannot open '%s': %s\n"),
				    path, strerror(errno));
				return (NULL);
			}
		}
	}

	/*
	 * Determine whether this is a device or a file.
	 */
	if (wholedisk || S_ISBLK(statbuf.st_mode)) {
		type = VDEV_TYPE_DISK;
	} else if (S_ISREG(statbuf.st_mode)) {
		type = VDEV_TYPE_FILE;
	} else {
		(void) fprintf(stderr, gettext("cannot use '%s': must be a "
		    "block device or regular file\n"), path);
		return (NULL);
	}

	/*
	 * Finally, we have the complete device or file, and we know that it is
	 * acceptable to use.  Construct the nvlist to describe this vdev.  All
	 * vdevs have a 'path' element, and devices also have a 'devid' element.
	 */
	verify(nvlist_alloc(&vdev, NV_UNIQUE_NAME, 0) == 0);
	verify(nvlist_add_string(vdev, ZPOOL_CONFIG_PATH, path) == 0);
	verify(nvlist_add_string(vdev, ZPOOL_CONFIG_TYPE, type) == 0);
	verify(nvlist_add_uint64(vdev, ZPOOL_CONFIG_IS_LOG, is_log) == 0);
	if (strcmp(type, VDEV_TYPE_DISK) == 0)
		verify(nvlist_add_uint64(vdev, ZPOOL_CONFIG_WHOLE_DISK,
		    (uint64_t)wholedisk) == 0);

	if (props != NULL) {
		uint64_t ashift = 0;
		char *value = NULL;

		if (nvlist_lookup_string(props,
		    zpool_prop_to_name(ZPOOL_PROP_ASHIFT), &value) == 0)
			zfs_nicestrtonum(NULL, value, &ashift);

		if (ashift > 0)
			verify(nvlist_add_uint64(vdev, ZPOOL_CONFIG_ASHIFT,
			    ashift) == 0);
	}

	return (vdev);
}

/*
 * Go through and verify the replication level of the pool is consistent.
 * Performs the following checks:
 *
 * 	For the new spec, verifies that devices in mirrors and raidz are the
 * 	same size.
 *
 * 	If the current configuration already has inconsistent replication
 * 	levels, ignore any other potential problems in the new spec.
 *
 * 	Otherwise, make sure that the current spec (if there is one) and the new
 * 	spec have consistent replication levels.
 */
typedef struct replication_level {
	char *zprl_type;
	uint64_t zprl_children;
	uint64_t zprl_parity;
} replication_level_t;

#define	ZPOOL_FUZZ	(16 * 1024 * 1024)

/*
 * Given a list of toplevel vdevs, return the current replication level.  If
 * the config is inconsistent, then NULL is returned.  If 'fatal' is set, then
 * an error message will be displayed for each self-inconsistent vdev.
 */
static replication_level_t *
get_replication(nvlist_t *nvroot, boolean_t fatal)
{
	nvlist_t **top;
	uint_t t, toplevels;
	nvlist_t **child;
	uint_t c, children;
	nvlist_t *nv;
	char *type;
	replication_level_t lastrep = { 0 }, rep, *ret;
	boolean_t dontreport;

	ret = safe_malloc(sizeof (replication_level_t));

	verify(nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    &top, &toplevels) == 0);

	lastrep.zprl_type = NULL;
	for (t = 0; t < toplevels; t++) {
		uint64_t is_log = B_FALSE;

		nv = top[t];

		/*
		 * For separate logs we ignore the top level vdev replication
		 * constraints.
		 */
		(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_IS_LOG, &is_log);
		if (is_log)
			continue;

		verify(nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE,
		    &type) == 0);
		if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
		    &child, &children) != 0) {
			/*
			 * This is a 'file' or 'disk' vdev.
			 */
			rep.zprl_type = type;
			rep.zprl_children = 1;
			rep.zprl_parity = 0;
		} else {
			uint64_t vdev_size;

			/*
			 * This is a mirror or RAID-Z vdev.  Go through and make
			 * sure the contents are all the same (files vs. disks),
			 * keeping track of the number of elements in the
			 * process.
			 *
			 * We also check that the size of each vdev (if it can
			 * be determined) is the same.
			 */
			rep.zprl_type = type;
			rep.zprl_children = 0;

			if (strcmp(type, VDEV_TYPE_RAIDZ) == 0) {
				verify(nvlist_lookup_uint64(nv,
				    ZPOOL_CONFIG_NPARITY,
				    &rep.zprl_parity) == 0);
				assert(rep.zprl_parity != 0);
			} else {
				rep.zprl_parity = 0;
			}

			/*
			 * The 'dontreport' variable indicates that we've
			 * already reported an error for this spec, so don't
			 * bother doing it again.
			 */
			type = NULL;
			dontreport = 0;
			vdev_size = -1ULL;
			for (c = 0; c < children; c++) {
				nvlist_t *cnv = child[c];
				char *path;
				struct stat64 statbuf;
				uint64_t size = -1ULL;
				char *childtype;
				int fd, err;

				rep.zprl_children++;

				verify(nvlist_lookup_string(cnv,
				    ZPOOL_CONFIG_TYPE, &childtype) == 0);

				/*
				 * If this is a replacing or spare vdev, then
				 * get the real first child of the vdev.
				 */
				if (strcmp(childtype,
				    VDEV_TYPE_REPLACING) == 0 ||
				    strcmp(childtype, VDEV_TYPE_SPARE) == 0) {
					nvlist_t **rchild;
					uint_t rchildren;

					verify(nvlist_lookup_nvlist_array(cnv,
					    ZPOOL_CONFIG_CHILDREN, &rchild,
					    &rchildren) == 0);
					assert(rchildren == 2);
					cnv = rchild[0];

					verify(nvlist_lookup_string(cnv,
					    ZPOOL_CONFIG_TYPE,
					    &childtype) == 0);
				}

				verify(nvlist_lookup_string(cnv,
				    ZPOOL_CONFIG_PATH, &path) == 0);

				/*
				 * If we have a raidz/mirror that combines disks
				 * with files, report it as an error.
				 */
				if (!dontreport && type != NULL &&
				    strcmp(type, childtype) != 0) {
					if (ret != NULL)
						free(ret);
					ret = NULL;
					if (fatal)
						vdev_error(gettext(
						    "mismatched replication "
						    "level: %s contains both "
						    "files and devices\n"),
						    rep.zprl_type);
					else
						return (NULL);
					dontreport = B_TRUE;
				}

				/*
				 * According to stat(2), the value of 'st_size'
				 * is undefined for block devices and character
				 * devices.  But there is no effective way to
				 * determine the real size in userland.
				 *
				 * Instead, we'll take advantage of an
				 * implementation detail of spec_size().  If the
				 * device is currently open, then we (should)
				 * return a valid size.
				 *
				 * If we still don't get a valid size (indicated
				 * by a size of 0 or MAXOFFSET_T), then ignore
				 * this device altogether.
				 */
				if ((fd = open(path, O_RDONLY)) >= 0) {
					err = fstat64(fd, &statbuf);
					(void) close(fd);
				} else {
					err = stat64(path, &statbuf);
				}

				if (err != 0 ||
				    statbuf.st_size == 0 ||
				    statbuf.st_size == MAXOFFSET_T)
					continue;

				size = statbuf.st_size;

				/*
				 * Also make sure that devices and
				 * slices have a consistent size.  If
				 * they differ by a significant amount
				 * (~16MB) then report an error.
				 */
				if (!dontreport &&
				    (vdev_size != -1ULL &&
				    (labs(size - vdev_size) >
				    ZPOOL_FUZZ))) {
					if (ret != NULL)
						free(ret);
					ret = NULL;
					if (fatal)
						vdev_error(gettext(
						    "%s contains devices of "
						    "different sizes\n"),
						    rep.zprl_type);
					else
						return (NULL);
					dontreport = B_TRUE;
				}

				type = childtype;
				vdev_size = size;
			}
		}

		/*
		 * At this point, we have the replication of the last toplevel
		 * vdev in 'rep'.  Compare it to 'lastrep' to see if its
		 * different.
		 */
		if (lastrep.zprl_type != NULL) {
			if (strcmp(lastrep.zprl_type, rep.zprl_type) != 0) {
				if (ret != NULL)
					free(ret);
				ret = NULL;
				if (fatal)
					vdev_error(gettext(
					    "mismatched replication level: "
					    "both %s and %s vdevs are "
					    "present\n"),
					    lastrep.zprl_type, rep.zprl_type);
				else
					return (NULL);
			} else if (lastrep.zprl_parity != rep.zprl_parity) {
				if (ret)
					free(ret);
				ret = NULL;
				if (fatal)
					vdev_error(gettext(
					    "mismatched replication level: "
					    "both %llu and %llu device parity "
					    "%s vdevs are present\n"),
					    lastrep.zprl_parity,
					    rep.zprl_parity,
					    rep.zprl_type);
				else
					return (NULL);
			} else if (lastrep.zprl_children != rep.zprl_children) {
				if (ret)
					free(ret);
				ret = NULL;
				if (fatal)
					vdev_error(gettext(
					    "mismatched replication level: "
					    "both %llu-way and %llu-way %s "
					    "vdevs are present\n"),
					    lastrep.zprl_children,
					    rep.zprl_children,
					    rep.zprl_type);
				else
					return (NULL);
			}
		}
		lastrep = rep;
	}

	if (ret != NULL)
		*ret = rep;

	return (ret);
}

/*
 * Check the replication level of the vdev spec against the current pool.  Calls
 * get_replication() to make sure the new spec is self-consistent.  If the pool
 * has a consistent replication level, then we ignore any errors.  Otherwise,
 * report any difference between the two.
 */
static int
check_replication(nvlist_t *config, nvlist_t *newroot)
{
	nvlist_t **child;
	uint_t	children;
	replication_level_t *current = NULL, *new;
	int ret;

	/*
	 * If we have a current pool configuration, check to see if it's
	 * self-consistent.  If not, simply return success.
	 */
	if (config != NULL) {
		nvlist_t *nvroot;

		verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
		    &nvroot) == 0);
		if ((current = get_replication(nvroot, B_FALSE)) == NULL)
			return (0);
	}
	/*
	 * for spares there may be no children, and therefore no
	 * replication level to check
	 */
	if ((nvlist_lookup_nvlist_array(newroot, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0) || (children == 0)) {
		free(current);
		return (0);
	}

	/*
	 * If all we have is logs then there's no replication level to check.
	 */
	if (num_logs(newroot) == children) {
		free(current);
		return (0);
	}

	/*
	 * Get the replication level of the new vdev spec, reporting any
	 * inconsistencies found.
	 */
	if ((new = get_replication(newroot, B_TRUE)) == NULL) {
		free(current);
		return (-1);
	}

	/*
	 * Check to see if the new vdev spec matches the replication level of
	 * the current pool.
	 */
	ret = 0;
	if (current != NULL) {
		if (strcmp(current->zprl_type, new->zprl_type) != 0) {
			vdev_error(gettext(
			    "mismatched replication level: pool uses %s "
			    "and new vdev is %s\n"),
			    current->zprl_type, new->zprl_type);
			ret = -1;
		} else if (current->zprl_parity != new->zprl_parity) {
			vdev_error(gettext(
			    "mismatched replication level: pool uses %llu "
			    "device parity and new vdev uses %llu\n"),
			    current->zprl_parity, new->zprl_parity);
			ret = -1;
		} else if (current->zprl_children != new->zprl_children) {
			vdev_error(gettext(
			    "mismatched replication level: pool uses %llu-way "
			    "%s and new vdev uses %llu-way %s\n"),
			    current->zprl_children, current->zprl_type,
			    new->zprl_children, new->zprl_type);
			ret = -1;
		}
	}

	free(new);
	if (current != NULL)
		free(current);

	return (ret);
}

static int
zero_label(char *path)
{
	const int size = 4096;
	char buf[size];
	int err, fd;

	if ((fd = open(path, O_WRONLY|O_EXCL)) < 0) {
		(void) fprintf(stderr, gettext("cannot open '%s': %s\n"),
		    path, strerror(errno));
		return (-1);
	}

	memset(buf, 0, size);
	err = write(fd, buf, size);
	(void) fdatasync(fd);
	(void) close(fd);

	if (err == -1) {
		(void) fprintf(stderr, gettext("cannot zero first %d bytes "
		    "of '%s': %s\n"), size, path, strerror(errno));
		return (-1);
	}

	if (err != size) {
		(void) fprintf(stderr, gettext("could only zero %d/%d bytes "
		    "of '%s'\n"), err, size, path);
		return (-1);
	}

	return 0;
}

/*
 * Go through and find any whole disks in the vdev specification, labelling them
 * as appropriate.  When constructing the vdev spec, we were unable to open this
 * device in order to provide a devid.  Now that we have labelled the disk and
 * know that slice 0 is valid, we can construct the devid now.
 *
 * If the disk was already labeled with an EFI label, we will have gotten the
 * devid already (because we were able to open the whole disk).  Otherwise, we
 * need to get the devid after we label the disk.
 */
static int
make_disks(zpool_handle_t *zhp, nvlist_t *nv)
{
	nvlist_t **child;
	uint_t c, children;
	char *type, *path, *diskname;
	char devpath[MAXPATHLEN];
	char udevpath[MAXPATHLEN];
	uint64_t wholedisk;
	struct stat64 statbuf;
	int ret;

	verify(nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type) == 0);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0) {

		if (strcmp(type, VDEV_TYPE_DISK) != 0)
			return (0);

		/*
		 * We have a disk device.  If this is a whole disk write
		 * out the efi partition table, otherwise write zero's to
		 * the first 4k of the partition.  This is to ensure that
		 * libblkid will not misidentify the partition due to a
		 * magic value left by the previous filesystem.
		 */
		verify(!nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path));
		verify(!nvlist_lookup_uint64(nv, ZPOOL_CONFIG_WHOLE_DISK,
		    &wholedisk));

		if (!wholedisk) {
			ret = zero_label(path);
			return (ret);
		}

		if (realpath(path, devpath) == NULL) {
			ret = errno;
			(void) fprintf(stderr,
			    gettext("cannot resolve path '%s'\n"), path);
			return (ret);
		}

		/*
		 * Remove any previously existing symlink from a udev path to
		 * the device before labeling the disk.  This makes
		 * zpool_label_disk_wait() truly wait for the new link to show
		 * up instead of returning if it finds an old link still in
		 * place.  Otherwise there is a window between when udev
		 * deletes and recreates the link during which access attempts
		 * will fail with ENOENT.
		 */
		strncpy(udevpath, path, MAXPATHLEN);
		(void) zfs_append_partition(udevpath, MAXPATHLEN);

		if ((strncmp(udevpath, UDISK_ROOT, strlen(UDISK_ROOT)) == 0) &&
		    (lstat64(udevpath, &statbuf) == 0) &&
		    S_ISLNK(statbuf.st_mode))
			(void) unlink(udevpath);

		diskname = strrchr(devpath, '/');
		assert(diskname != NULL);
		diskname++;
		if (zpool_label_disk(g_zfs, zhp, diskname) == -1)
			return (-1);

		/*
		 * Now we've labeled the disk and the partitions have been
		 * created.  We still need to wait for udev to create the
		 * symlinks to those partitions.
		 */
		if ((ret = zpool_label_disk_wait(udevpath, 1000)) != 0) {
			(void) fprintf(stderr,
			    gettext( "cannot resolve path '%s'\n"), udevpath);
			return (-1);
		}

		/*
		 * Update the path to refer to the partition.  The presence of
		 * the 'whole_disk' field indicates to the CLI that we should
		 * chop off the partition number when displaying the device in
		 * future output.
		 */
		verify(nvlist_add_string(nv, ZPOOL_CONFIG_PATH, udevpath) == 0);

		/* Just in case this partition already existed. */
		(void) zero_label(udevpath);

		return (0);
	}

	for (c = 0; c < children; c++)
		if ((ret = make_disks(zhp, child[c])) != 0)
			return (ret);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_SPARES,
	    &child, &children) == 0)
		for (c = 0; c < children; c++)
			if ((ret = make_disks(zhp, child[c])) != 0)
				return (ret);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) == 0)
		for (c = 0; c < children; c++)
			if ((ret = make_disks(zhp, child[c])) != 0)
				return (ret);

	return (0);
}

/*
 * Determine if the given path is a hot spare within the given configuration.
 */
static boolean_t
is_spare(nvlist_t *config, const char *path)
{
	int fd;
	pool_state_t state;
	char *name = NULL;
	nvlist_t *label;
	uint64_t guid, spareguid;
	nvlist_t *nvroot;
	nvlist_t **spares;
	uint_t i, nspares;
	boolean_t inuse;

	if ((fd = open(path, O_RDONLY|O_EXCL)) < 0)
		return (B_FALSE);

	if (zpool_in_use(g_zfs, fd, &state, &name, &inuse) != 0 ||
	    !inuse ||
	    state != POOL_STATE_SPARE ||
	    zpool_read_label(fd, &label) != 0) {
		free(name);
		(void) close(fd);
		return (B_FALSE);
	}
	free(name);
	(void) close(fd);

	verify(nvlist_lookup_uint64(label, ZPOOL_CONFIG_GUID, &guid) == 0);
	nvlist_free(label);

	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);
	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
	    &spares, &nspares) == 0) {
		for (i = 0; i < nspares; i++) {
			verify(nvlist_lookup_uint64(spares[i],
			    ZPOOL_CONFIG_GUID, &spareguid) == 0);
			if (spareguid == guid)
				return (B_TRUE);
		}
	}

	return (B_FALSE);
}

/*
 * Go through and find any devices that are in use.  We rely on libdiskmgt for
 * the majority of this task.
 */
static int
check_in_use(nvlist_t *config, nvlist_t *nv, boolean_t force,
    boolean_t replacing, boolean_t isspare)
{
	nvlist_t **child;
	uint_t c, children;
	char *type, *path;
	int ret = 0;
	char buf[MAXPATHLEN];
	uint64_t wholedisk = B_FALSE;

	verify(nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type) == 0);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0) {

		verify(!nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path));
		if (strcmp(type, VDEV_TYPE_DISK) == 0)
			verify(!nvlist_lookup_uint64(nv,
			       ZPOOL_CONFIG_WHOLE_DISK, &wholedisk));

		/*
		 * As a generic check, we look to see if this is a replace of a
		 * hot spare within the same pool.  If so, we allow it
		 * regardless of what libblkid or zpool_in_use() says.
		 */
		if (replacing) {
			if (wholedisk)
				(void) snprintf(buf, sizeof (buf), "%ss0",
				    path);
			else
				(void) strlcpy(buf, path, sizeof (buf));

			if (is_spare(config, buf))
				return (0);
		}

		if (strcmp(type, VDEV_TYPE_DISK) == 0)
			ret = check_device(path, force, isspare, wholedisk);

		if (strcmp(type, VDEV_TYPE_FILE) == 0)
			ret = check_file(path, force, isspare);

		return (ret);
	}

	for (c = 0; c < children; c++)
		if ((ret = check_in_use(config, child[c], force,
		    replacing, B_FALSE)) != 0)
			return (ret);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_SPARES,
	    &child, &children) == 0)
		for (c = 0; c < children; c++)
			if ((ret = check_in_use(config, child[c], force,
			    replacing, B_TRUE)) != 0)
				return (ret);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) == 0)
		for (c = 0; c < children; c++)
			if ((ret = check_in_use(config, child[c], force,
			    replacing, B_FALSE)) != 0)
				return (ret);

	return (0);
}

static const char *
is_grouping(const char *type, int *mindev, int *maxdev)
{
	if (strncmp(type, "raidz", 5) == 0) {
		const char *p = type + 5;
		char *end;
		long nparity;

		if (*p == '\0') {
			nparity = 1;
		} else if (*p == '0') {
			return (NULL); /* no zero prefixes allowed */
		} else {
			errno = 0;
			nparity = strtol(p, &end, 10);
			if (errno != 0 || nparity < 1 || nparity >= 255 ||
			    *end != '\0')
				return (NULL);
		}

		if (mindev != NULL)
			*mindev = nparity + 1;
		if (maxdev != NULL)
			*maxdev = 255;
		return (VDEV_TYPE_RAIDZ);
	}

	if (maxdev != NULL)
		*maxdev = INT_MAX;

	if (strcmp(type, "mirror") == 0) {
		if (mindev != NULL)
			*mindev = 2;
		return (VDEV_TYPE_MIRROR);
	}

	if (strcmp(type, "spare") == 0) {
		if (mindev != NULL)
			*mindev = 1;
		return (VDEV_TYPE_SPARE);
	}

	if (strcmp(type, "log") == 0) {
		if (mindev != NULL)
			*mindev = 1;
		return (VDEV_TYPE_LOG);
	}

	if (strcmp(type, "cache") == 0) {
		if (mindev != NULL)
			*mindev = 1;
		return (VDEV_TYPE_L2CACHE);
	}

	return (NULL);
}

/*
 * Construct a syntactically valid vdev specification,
 * and ensure that all devices and files exist and can be opened.
 * Note: we don't bother freeing anything in the error paths
 * because the program is just going to exit anyway.
 */
nvlist_t *
construct_spec(nvlist_t *props, int argc, char **argv)
{
	nvlist_t *nvroot, *nv, **top, **spares, **l2cache;
	int t, toplevels, mindev, maxdev, nspares, nlogs, nl2cache;
	const char *type;
	uint64_t is_log;
	boolean_t seen_logs;

	top = NULL;
	toplevels = 0;
	spares = NULL;
	l2cache = NULL;
	nspares = 0;
	nlogs = 0;
	nl2cache = 0;
	is_log = B_FALSE;
	seen_logs = B_FALSE;

	while (argc > 0) {
		nv = NULL;

		/*
		 * If it's a mirror or raidz, the subsequent arguments are
		 * its leaves -- until we encounter the next mirror or raidz.
		 */
		if ((type = is_grouping(argv[0], &mindev, &maxdev)) != NULL) {
			nvlist_t **child = NULL;
			int c, children = 0;

			if (strcmp(type, VDEV_TYPE_SPARE) == 0) {
				if (spares != NULL) {
					(void) fprintf(stderr,
					    gettext("invalid vdev "
					    "specification: 'spare' can be "
					    "specified only once\n"));
					return (NULL);
				}
				is_log = B_FALSE;
			}

			if (strcmp(type, VDEV_TYPE_LOG) == 0) {
				if (seen_logs) {
					(void) fprintf(stderr,
					    gettext("invalid vdev "
					    "specification: 'log' can be "
					    "specified only once\n"));
					return (NULL);
				}
				seen_logs = B_TRUE;
				is_log = B_TRUE;
				argc--;
				argv++;
				/*
				 * A log is not a real grouping device.
				 * We just set is_log and continue.
				 */
				continue;
			}

			if (strcmp(type, VDEV_TYPE_L2CACHE) == 0) {
				if (l2cache != NULL) {
					(void) fprintf(stderr,
					    gettext("invalid vdev "
					    "specification: 'cache' can be "
					    "specified only once\n"));
					return (NULL);
				}
				is_log = B_FALSE;
			}

			if (is_log) {
				if (strcmp(type, VDEV_TYPE_MIRROR) != 0) {
					(void) fprintf(stderr,
					    gettext("invalid vdev "
					    "specification: unsupported 'log' "
					    "device: %s\n"), type);
					return (NULL);
				}
				nlogs++;
			}

			for (c = 1; c < argc; c++) {
				if (is_grouping(argv[c], NULL, NULL) != NULL)
					break;
				children++;
				child = realloc(child,
				    children * sizeof (nvlist_t *));
				if (child == NULL)
					zpool_no_memory();
				if ((nv = make_leaf_vdev(props, argv[c], B_FALSE))
				    == NULL)
					return (NULL);
				child[children - 1] = nv;
			}

			if (children < mindev) {
				(void) fprintf(stderr, gettext("invalid vdev "
				    "specification: %s requires at least %d "
				    "devices\n"), argv[0], mindev);
				return (NULL);
			}

			if (children > maxdev) {
				(void) fprintf(stderr, gettext("invalid vdev "
				    "specification: %s supports no more than "
				    "%d devices\n"), argv[0], maxdev);
				return (NULL);
			}

			argc -= c;
			argv += c;

			if (strcmp(type, VDEV_TYPE_SPARE) == 0) {
				spares = child;
				nspares = children;
				continue;
			} else if (strcmp(type, VDEV_TYPE_L2CACHE) == 0) {
				l2cache = child;
				nl2cache = children;
				continue;
			} else {
				verify(nvlist_alloc(&nv, NV_UNIQUE_NAME,
				    0) == 0);
				verify(nvlist_add_string(nv, ZPOOL_CONFIG_TYPE,
				    type) == 0);
				verify(nvlist_add_uint64(nv,
				    ZPOOL_CONFIG_IS_LOG, is_log) == 0);
				if (strcmp(type, VDEV_TYPE_RAIDZ) == 0) {
					verify(nvlist_add_uint64(nv,
					    ZPOOL_CONFIG_NPARITY,
					    mindev - 1) == 0);
				}
				verify(nvlist_add_nvlist_array(nv,
				    ZPOOL_CONFIG_CHILDREN, child,
				    children) == 0);

				for (c = 0; c < children; c++)
					nvlist_free(child[c]);
				free(child);
			}
		} else {
			/*
			 * We have a device.  Pass off to make_leaf_vdev() to
			 * construct the appropriate nvlist describing the vdev.
			 */
			if ((nv = make_leaf_vdev(props, argv[0], is_log)) == NULL)
				return (NULL);
			if (is_log)
				nlogs++;
			argc--;
			argv++;
		}

		toplevels++;
		top = realloc(top, toplevels * sizeof (nvlist_t *));
		if (top == NULL)
			zpool_no_memory();
		top[toplevels - 1] = nv;
	}

	if (toplevels == 0 && nspares == 0 && nl2cache == 0) {
		(void) fprintf(stderr, gettext("invalid vdev "
		    "specification: at least one toplevel vdev must be "
		    "specified\n"));
		return (NULL);
	}

	if (seen_logs && nlogs == 0) {
		(void) fprintf(stderr, gettext("invalid vdev specification: "
		    "log requires at least 1 device\n"));
		return (NULL);
	}

	/*
	 * Finally, create nvroot and add all top-level vdevs to it.
	 */
	verify(nvlist_alloc(&nvroot, NV_UNIQUE_NAME, 0) == 0);
	verify(nvlist_add_string(nvroot, ZPOOL_CONFIG_TYPE,
	    VDEV_TYPE_ROOT) == 0);
	verify(nvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    top, toplevels) == 0);
	if (nspares != 0)
		verify(nvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
		    spares, nspares) == 0);
	if (nl2cache != 0)
		verify(nvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
		    l2cache, nl2cache) == 0);

	for (t = 0; t < toplevels; t++)
		nvlist_free(top[t]);
	for (t = 0; t < nspares; t++)
		nvlist_free(spares[t]);
	for (t = 0; t < nl2cache; t++)
		nvlist_free(l2cache[t]);
	if (spares)
		free(spares);
	if (l2cache)
		free(l2cache);
	free(top);

	return (nvroot);
}

nvlist_t *
split_mirror_vdev(zpool_handle_t *zhp, char *newname, nvlist_t *props,
    splitflags_t flags, int argc, char **argv)
{
	nvlist_t *newroot = NULL, **child;
	uint_t c, children;

	if (argc > 0) {
		if ((newroot = construct_spec(props, argc, argv)) == NULL) {
			(void) fprintf(stderr, gettext("Unable to build a "
			    "pool from the specified devices\n"));
			return (NULL);
		}

		if (!flags.dryrun && make_disks(zhp, newroot) != 0) {
			nvlist_free(newroot);
			return (NULL);
		}

		/* avoid any tricks in the spec */
		verify(nvlist_lookup_nvlist_array(newroot,
		    ZPOOL_CONFIG_CHILDREN, &child, &children) == 0);
		for (c = 0; c < children; c++) {
			char *path;
			const char *type;
			int min, max;

			verify(nvlist_lookup_string(child[c],
			    ZPOOL_CONFIG_PATH, &path) == 0);
			if ((type = is_grouping(path, &min, &max)) != NULL) {
				(void) fprintf(stderr, gettext("Cannot use "
				    "'%s' as a device for splitting\n"), type);
				nvlist_free(newroot);
				return (NULL);
			}
		}
	}

	if (zpool_vdev_split(zhp, newname, &newroot, props, flags) != 0) {
		if (newroot != NULL)
			nvlist_free(newroot);
		return (NULL);
	}

	return (newroot);
}

/*
 * Get and validate the contents of the given vdev specification.  This ensures
 * that the nvlist returned is well-formed, that all the devices exist, and that
 * they are not currently in use by any other known consumer.  The 'poolconfig'
 * parameter is the current configuration of the pool when adding devices
 * existing pool, and is used to perform additional checks, such as changing the
 * replication level of the pool.  It can be 'NULL' to indicate that this is a
 * new pool.  The 'force' flag controls whether devices should be forcefully
 * added, even if they appear in use.
 */
nvlist_t *
make_root_vdev(zpool_handle_t *zhp, nvlist_t *props, int force, int check_rep,
    boolean_t replacing, boolean_t dryrun, int argc, char **argv)
{
	nvlist_t *newroot;
	nvlist_t *poolconfig = NULL;
	is_force = force;

	/*
	 * Construct the vdev specification.  If this is successful, we know
	 * that we have a valid specification, and that all devices can be
	 * opened.
	 */
	if ((newroot = construct_spec(props, argc, argv)) == NULL)
		return (NULL);

	if (zhp && ((poolconfig = zpool_get_config(zhp, NULL)) == NULL))
		return (NULL);

	/*
	 * Validate each device to make sure that its not shared with another
	 * subsystem.  We do this even if 'force' is set, because there are some
	 * uses (such as a dedicated dump device) that even '-f' cannot
	 * override.
	 */
	if (check_in_use(poolconfig, newroot, force, replacing, B_FALSE) != 0) {
		nvlist_free(newroot);
		return (NULL);
	}

	/*
	 * Check the replication level of the given vdevs and report any errors
	 * found.  We include the existing pool spec, if any, as we need to
	 * catch changes against the existing replication level.
	 */
	if (check_rep && check_replication(poolconfig, newroot) != 0) {
		nvlist_free(newroot);
		return (NULL);
	}

	/*
	 * Run through the vdev specification and label any whole disks found.
	 */
	if (!dryrun && make_disks(zhp, newroot) != 0) {
		nvlist_free(newroot);
		return (NULL);
	}

	return (newroot);
}
