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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2016, 2017 Intel Corporation.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>.
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
#include <errno.h>
#include <fcntl.h>
#include <libintl.h>
#include <libnvpair.h>
#include <libzutil.h>
#include <limits.h>
#include <sys/spa.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "zpool_util.h"
#include <sys/zfs_context.h>
#include <sys/stat.h>

/*
 * For any given vdev specification, we can have multiple errors.  The
 * vdev_error() function keeps track of whether we have seen an error yet, and
 * prints out a header if its the first error we've seen.
 */
boolean_t error_seen;
boolean_t is_force;

void
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
int
check_file_generic(const char *file, boolean_t force, boolean_t isspare)
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
		if (state == POOL_STATE_SPARE && isspare) {
			free(name);
			(void) close(fd);
			return (0);
		}

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

/*
 * This may be a shorthand device path or it could be total gibberish.
 * Check to see if it is a known device available in zfs_vdev_paths.
 * As part of this check, see if we've been given an entire disk
 * (minus the slice number).
 */
static int
is_shorthand_path(const char *arg, char *path, size_t path_size,
    struct stat64 *statbuf, boolean_t *wholedisk)
{
	int error;

	error = zfs_resolve_shortname(arg, path, path_size);
	if (error == 0) {
		*wholedisk = zfs_dev_is_whole_disk(path);
		if (*wholedisk || (stat64(path, statbuf) == 0))
			return (0);
	}

	strlcpy(path, arg, path_size);
	memset(statbuf, 0, sizeof (*statbuf));
	*wholedisk = B_FALSE;

	return (error);
}

/*
 * Determine if the given path is a hot spare within the given configuration.
 * If no configuration is given we rely solely on the label.
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

	if (zpool_is_draid_spare(path))
		return (B_TRUE);

	if ((fd = open(path, O_RDONLY|O_DIRECT)) < 0)
		return (B_FALSE);

	if (zpool_in_use(g_zfs, fd, &state, &name, &inuse) != 0 ||
	    !inuse ||
	    state != POOL_STATE_SPARE ||
	    zpool_read_label(fd, &label, NULL) != 0) {
		free(name);
		(void) close(fd);
		return (B_FALSE);
	}
	free(name);
	(void) close(fd);

	if (config == NULL) {
		nvlist_free(label);
		return (B_TRUE);
	}

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
 * Create a leaf vdev.  Determine if this is a file or a device.  If it's a
 * device, fill in the device id to make a complete nvlist.  Valid forms for a
 * leaf vdev are:
 *
 *	/dev/xxx	Complete disk path
 *	/xxx		Full path to file
 *	xxx		Shorthand for <zfs_vdev_paths>/xxx
 *	draid*		Virtual dRAID spare
 */
static nvlist_t *
make_leaf_vdev(nvlist_t *props, const char *arg, boolean_t is_primary)
{
	char path[MAXPATHLEN];
	struct stat64 statbuf;
	nvlist_t *vdev = NULL;
	const char *type = NULL;
	boolean_t wholedisk = B_FALSE;
	uint64_t ashift = 0;
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
		 * are resolved to their real paths to determine whole disk
		 * and S_ISBLK/S_ISREG type checks.  However, we are careful
		 * to store the given path as ZPOOL_CONFIG_PATH to ensure we
		 * can leverage udev's persistent device labels.
		 */
		if (realpath(arg, path) == NULL) {
			(void) fprintf(stderr,
			    gettext("cannot resolve path '%s'\n"), arg);
			return (NULL);
		}

		wholedisk = zfs_dev_is_whole_disk(path);
		if (!wholedisk && (stat64(path, &statbuf) != 0)) {
			(void) fprintf(stderr,
			    gettext("cannot open '%s': %s\n"),
			    path, strerror(errno));
			return (NULL);
		}

		/* After whole disk check restore original passed path */
		strlcpy(path, arg, sizeof (path));
	} else if (zpool_is_draid_spare(arg)) {
		if (!is_primary) {
			(void) fprintf(stderr,
			    gettext("cannot open '%s': dRAID spares can only "
			    "be used to replace primary vdevs\n"), arg);
			return (NULL);
		}

		wholedisk = B_TRUE;
		strlcpy(path, arg, sizeof (path));
		type = VDEV_TYPE_DRAID_SPARE;
	} else {
		err = is_shorthand_path(arg, path, sizeof (path),
		    &statbuf, &wholedisk);
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

	if (type == NULL) {
		/*
		 * Determine whether this is a device or a file.
		 */
		if (wholedisk || S_ISBLK(statbuf.st_mode)) {
			type = VDEV_TYPE_DISK;
		} else if (S_ISREG(statbuf.st_mode)) {
			type = VDEV_TYPE_FILE;
		} else {
			fprintf(stderr, gettext("cannot use '%s': must "
			    "be a block device or regular file\n"), path);
			return (NULL);
		}
	}

	/*
	 * Finally, we have the complete device or file, and we know that it is
	 * acceptable to use.  Construct the nvlist to describe this vdev.  All
	 * vdevs have a 'path' element, and devices also have a 'devid' element.
	 */
	verify(nvlist_alloc(&vdev, NV_UNIQUE_NAME, 0) == 0);
	verify(nvlist_add_string(vdev, ZPOOL_CONFIG_PATH, path) == 0);
	verify(nvlist_add_string(vdev, ZPOOL_CONFIG_TYPE, type) == 0);

	if (strcmp(type, VDEV_TYPE_DISK) == 0)
		verify(nvlist_add_uint64(vdev, ZPOOL_CONFIG_WHOLE_DISK,
		    (uint64_t)wholedisk) == 0);

	/*
	 * Override defaults if custom properties are provided.
	 */
	if (props != NULL) {
		const char *value = NULL;

		if (nvlist_lookup_string(props,
		    zpool_prop_to_name(ZPOOL_PROP_ASHIFT), &value) == 0) {
			if (zfs_nicestrtonum(NULL, value, &ashift) != 0) {
				(void) fprintf(stderr,
				    gettext("ashift must be a number.\n"));
				return (NULL);
			}
			if (ashift != 0 &&
			    (ashift < ASHIFT_MIN || ashift > ASHIFT_MAX)) {
				(void) fprintf(stderr,
				    gettext("invalid 'ashift=%" PRIu64 "' "
				    "property: only values between %" PRId32 " "
				    "and %" PRId32 " are allowed.\n"),
				    ashift, ASHIFT_MIN, ASHIFT_MAX);
				return (NULL);
			}
		}
	}

	/*
	 * If the device is known to incorrectly report its physical sector
	 * size explicitly provide the known correct value.
	 */
	if (ashift == 0) {
		int sector_size;

		if (check_sector_size_database(path, &sector_size) == B_TRUE)
			ashift = highbit64(sector_size) - 1;
	}

	if (ashift > 0)
		(void) nvlist_add_uint64(vdev, ZPOOL_CONFIG_ASHIFT, ashift);

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
 *
 *	If there is no current spec (create), make sure new spec has at least
 *	one general purpose vdev.
 */
typedef struct replication_level {
	const char *zprl_type;
	uint64_t zprl_children;
	uint64_t zprl_parity;
} replication_level_t;

#define	ZPOOL_FUZZ	(16 * 1024 * 1024)

/*
 * N.B. For the purposes of comparing replication levels dRAID can be
 * considered functionally equivalent to raidz.
 */
static boolean_t
is_raidz_mirror(replication_level_t *a, replication_level_t *b,
    replication_level_t **raidz, replication_level_t **mirror)
{
	if ((strcmp(a->zprl_type, "raidz") == 0 ||
	    strcmp(a->zprl_type, "draid") == 0) &&
	    strcmp(b->zprl_type, "mirror") == 0) {
		*raidz = a;
		*mirror = b;
		return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Comparison for determining if dRAID and raidz where passed in either order.
 */
static boolean_t
is_raidz_draid(replication_level_t *a, replication_level_t *b)
{
	if ((strcmp(a->zprl_type, "raidz") == 0 ||
	    strcmp(a->zprl_type, "draid") == 0) &&
	    (strcmp(b->zprl_type, "raidz") == 0 ||
	    strcmp(b->zprl_type, "draid") == 0)) {
		return (B_TRUE);
	}

	return (B_FALSE);
}

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
	const char *type;
	replication_level_t lastrep = {0};
	replication_level_t rep;
	replication_level_t *ret;
	replication_level_t *raidz, *mirror;
	boolean_t dontreport;

	ret = safe_malloc(sizeof (replication_level_t));

	verify(nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    &top, &toplevels) == 0);

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

		/*
		 * Ignore holes introduced by removing aux devices, along
		 * with indirect vdevs introduced by previously removed
		 * vdevs.
		 */
		verify(nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type) == 0);
		if (strcmp(type, VDEV_TYPE_HOLE) == 0 ||
		    strcmp(type, VDEV_TYPE_INDIRECT) == 0)
			continue;

		if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
		    &child, &children) != 0) {
			/*
			 * This is a 'file' or 'disk' vdev.
			 */
			rep.zprl_type = type;
			rep.zprl_children = 1;
			rep.zprl_parity = 0;
		} else {
			int64_t vdev_size;

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

			if (strcmp(type, VDEV_TYPE_RAIDZ) == 0 ||
			    strcmp(type, VDEV_TYPE_DRAID) == 0) {
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
			vdev_size = -1LL;
			for (c = 0; c < children; c++) {
				nvlist_t *cnv = child[c];
				const char *path;
				struct stat64 statbuf;
				int64_t size = -1LL;
				const char *childtype;
				int fd, err;

				rep.zprl_children++;

				verify(nvlist_lookup_string(cnv,
				    ZPOOL_CONFIG_TYPE, &childtype) == 0);

				/*
				 * If this is a replacing or spare vdev, then
				 * get the real first child of the vdev: do this
				 * in a loop because replacing and spare vdevs
				 * can be nested.
				 */
				while (strcmp(childtype,
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
					err = fstat64_blk(fd, &statbuf);
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
				    (vdev_size != -1LL &&
				    (llabs(size - vdev_size) >
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
		 * vdev in 'rep'.  Compare it to 'lastrep' to see if it is
		 * different.
		 */
		if (lastrep.zprl_type != NULL) {
			if (is_raidz_mirror(&lastrep, &rep, &raidz, &mirror) ||
			    is_raidz_mirror(&rep, &lastrep, &raidz, &mirror)) {
				/*
				 * Accepted raidz and mirror when they can
				 * handle the same number of disk failures.
				 */
				if (raidz->zprl_parity !=
				    mirror->zprl_children - 1) {
					if (ret != NULL)
						free(ret);
					ret = NULL;
					if (fatal)
						vdev_error(gettext(
						    "mismatched replication "
						    "level: "
						    "%s and %s vdevs with "
						    "different redundancy, "
						    "%llu vs. %llu (%llu-way) "
						    "are present\n"),
						    raidz->zprl_type,
						    mirror->zprl_type,
						    (u_longlong_t)
						    raidz->zprl_parity,
						    (u_longlong_t)
						    mirror->zprl_children - 1,
						    (u_longlong_t)
						    mirror->zprl_children);
					else
						return (NULL);
				}
			} else if (is_raidz_draid(&lastrep, &rep)) {
				/*
				 * Accepted raidz and draid when they can
				 * handle the same number of disk failures.
				 */
				if (lastrep.zprl_parity != rep.zprl_parity) {
					if (ret != NULL)
						free(ret);
					ret = NULL;
					if (fatal)
						vdev_error(gettext(
						    "mismatched replication "
						    "level: %s and %s vdevs "
						    "with different "
						    "redundancy, %llu vs. "
						    "%llu are present\n"),
						    lastrep.zprl_type,
						    rep.zprl_type,
						    (u_longlong_t)
						    lastrep.zprl_parity,
						    (u_longlong_t)
						    rep.zprl_parity);
					else
						return (NULL);
				}
			} else if (strcmp(lastrep.zprl_type, rep.zprl_type) !=
			    0) {
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
					    (u_longlong_t)
					    lastrep.zprl_parity,
					    (u_longlong_t)rep.zprl_parity,
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
					    (u_longlong_t)
					    lastrep.zprl_children,
					    (u_longlong_t)
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
	replication_level_t *raidz, *mirror;
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
		if (is_raidz_mirror(current, new, &raidz, &mirror) ||
		    is_raidz_mirror(new, current, &raidz, &mirror)) {
			if (raidz->zprl_parity != mirror->zprl_children - 1) {
				vdev_error(gettext(
				    "mismatched replication level: pool and "
				    "new vdev with different redundancy, %s "
				    "and %s vdevs, %llu vs. %llu (%llu-way)\n"),
				    raidz->zprl_type,
				    mirror->zprl_type,
				    (u_longlong_t)raidz->zprl_parity,
				    (u_longlong_t)mirror->zprl_children - 1,
				    (u_longlong_t)mirror->zprl_children);
				ret = -1;
			}
		} else if (strcmp(current->zprl_type, new->zprl_type) != 0) {
			vdev_error(gettext(
			    "mismatched replication level: pool uses %s "
			    "and new vdev is %s\n"),
			    current->zprl_type, new->zprl_type);
			ret = -1;
		} else if (current->zprl_parity != new->zprl_parity) {
			vdev_error(gettext(
			    "mismatched replication level: pool uses %llu "
			    "device parity and new vdev uses %llu\n"),
			    (u_longlong_t)current->zprl_parity,
			    (u_longlong_t)new->zprl_parity);
			ret = -1;
		} else if (current->zprl_children != new->zprl_children) {
			vdev_error(gettext(
			    "mismatched replication level: pool uses %llu-way "
			    "%s and new vdev uses %llu-way %s\n"),
			    (u_longlong_t)current->zprl_children,
			    current->zprl_type,
			    (u_longlong_t)new->zprl_children,
			    new->zprl_type);
			ret = -1;
		}
	}

	free(new);
	if (current != NULL)
		free(current);

	return (ret);
}

static int
zero_label(const char *path)
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

	return (0);
}

static void
lines_to_stderr(char *lines[], int lines_cnt)
{
	int i;
	for (i = 0; i < lines_cnt; i++) {
		fprintf(stderr, "%s\n", lines[i]);
	}
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
make_disks(zpool_handle_t *zhp, nvlist_t *nv, boolean_t replacing)
{
	nvlist_t **child;
	uint_t c, children;
	const char *type, *path;
	char devpath[MAXPATHLEN];
	char udevpath[MAXPATHLEN];
	uint64_t wholedisk;
	struct stat64 statbuf;
	int is_exclusive = 0;
	int fd;
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
			/*
			 * Update device id string for mpath nodes (Linux only)
			 */
			if (is_mpath_whole_disk(path))
				update_vdev_config_dev_strs(nv);

			if (!is_spare(NULL, path))
				(void) zero_label(path);
			return (0);
		}

		if (realpath(path, devpath) == NULL) {
			ret = errno;
			(void) fprintf(stderr,
			    gettext("cannot resolve path '%s'\n"), path);
			return (ret);
		}

		/*
		 * Remove any previously existing symlink from a udev path to
		 * the device before labeling the disk.  This ensures that
		 * only newly created links are used.  Otherwise there is a
		 * window between when udev deletes and recreates the link
		 * during which access attempts will fail with ENOENT.
		 */
		strlcpy(udevpath, path, MAXPATHLEN);
		(void) zfs_append_partition(udevpath, MAXPATHLEN);

		fd = open(devpath, O_RDWR|O_EXCL);
		if (fd == -1) {
			if (errno == EBUSY)
				is_exclusive = 1;
#ifdef __FreeBSD__
			if (errno == EPERM)
				is_exclusive = 1;
#endif
		} else {
			(void) close(fd);
		}

		/*
		 * If the partition exists, contains a valid spare label,
		 * and is opened exclusively there is no need to partition
		 * it.  Hot spares have already been partitioned and are
		 * held open exclusively by the kernel as a safety measure.
		 *
		 * If the provided path is for a /dev/disk/ device its
		 * symbolic link will be removed, partition table created,
		 * and then block until udev creates the new link.
		 */
		if (!is_exclusive && !is_spare(NULL, udevpath)) {
			char *devnode = strrchr(devpath, '/') + 1;
			char **lines = NULL;
			int lines_cnt = 0;

			ret = strncmp(udevpath, UDISK_ROOT, strlen(UDISK_ROOT));
			if (ret == 0) {
				ret = lstat64(udevpath, &statbuf);
				if (ret == 0 && S_ISLNK(statbuf.st_mode))
					(void) unlink(udevpath);
			}

			/*
			 * When labeling a pool the raw device node name
			 * is provided as it appears under /dev/.
			 *
			 * Note that 'zhp' will be NULL when we're creating a
			 * pool.
			 */
			if (zpool_prepare_and_label_disk(g_zfs, zhp, devnode,
			    nv, zhp == NULL ? "create" :
			    replacing ? "replace" : "add", &lines,
			    &lines_cnt) != 0) {
				(void) fprintf(stderr,
				    gettext(
				    "Error preparing/labeling disk.\n"));
				if (lines_cnt > 0) {
					(void) fprintf(stderr,
					gettext("zfs_prepare_disk output:\n"));
					lines_to_stderr(lines, lines_cnt);
				}

				libzfs_free_str_array(lines, lines_cnt);
				return (-1);
			}
			libzfs_free_str_array(lines, lines_cnt);

			/*
			 * Wait for udev to signal the device is available
			 * by the provided path.
			 */
			ret = zpool_label_disk_wait(udevpath, DISK_LABEL_WAIT);
			if (ret) {
				(void) fprintf(stderr,
				    gettext("missing link: %s was "
				    "partitioned but %s is missing\n"),
				    devnode, udevpath);
				return (ret);
			}

			ret = zero_label(udevpath);
			if (ret)
				return (ret);
		}

		/*
		 * Update the path to refer to the partition.  The presence of
		 * the 'whole_disk' field indicates to the CLI that we should
		 * chop off the partition number when displaying the device in
		 * future output.
		 */
		verify(nvlist_add_string(nv, ZPOOL_CONFIG_PATH, udevpath) == 0);

		/*
		 * Update device id strings for whole disks (Linux only)
		 */
		update_vdev_config_dev_strs(nv);

		return (0);
	}

	for (c = 0; c < children; c++)
		if ((ret = make_disks(zhp, child[c], replacing)) != 0)
			return (ret);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_SPARES,
	    &child, &children) == 0)
		for (c = 0; c < children; c++)
			if ((ret = make_disks(zhp, child[c], replacing)) != 0)
				return (ret);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) == 0)
		for (c = 0; c < children; c++)
			if ((ret = make_disks(zhp, child[c], replacing)) != 0)
				return (ret);

	return (0);
}

/*
 * Go through and find any devices that are in use.  We rely on libdiskmgt for
 * the majority of this task.
 */
static boolean_t
is_device_in_use(nvlist_t *config, nvlist_t *nv, boolean_t force,
    boolean_t replacing, boolean_t isspare)
{
	nvlist_t **child;
	uint_t c, children;
	const char *type, *path;
	int ret = 0;
	char buf[MAXPATHLEN];
	uint64_t wholedisk = B_FALSE;
	boolean_t anyinuse = B_FALSE;

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
			(void) strlcpy(buf, path, sizeof (buf));
			if (wholedisk) {
				ret = zfs_append_partition(buf,  sizeof (buf));
				if (ret == -1)
					return (-1);
			}

			if (is_spare(config, buf))
				return (B_FALSE);
		}

		if (strcmp(type, VDEV_TYPE_DISK) == 0)
			ret = check_device(path, force, isspare, wholedisk);

		else if (strcmp(type, VDEV_TYPE_FILE) == 0)
			ret = check_file(path, force, isspare);

		return (ret != 0);
	}

	for (c = 0; c < children; c++)
		if (is_device_in_use(config, child[c], force, replacing,
		    B_FALSE))
			anyinuse = B_TRUE;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_SPARES,
	    &child, &children) == 0)
		for (c = 0; c < children; c++)
			if (is_device_in_use(config, child[c], force, replacing,
			    B_TRUE))
				anyinuse = B_TRUE;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) == 0)
		for (c = 0; c < children; c++)
			if (is_device_in_use(config, child[c], force, replacing,
			    B_FALSE))
				anyinuse = B_TRUE;

	return (anyinuse);
}

/*
 * Returns the parity level extracted from a raidz or draid type.
 * If the parity cannot be determined zero is returned.
 */
static int
get_parity(const char *type)
{
	long parity = 0;
	const char *p;

	if (strncmp(type, VDEV_TYPE_RAIDZ, strlen(VDEV_TYPE_RAIDZ)) == 0) {
		p = type + strlen(VDEV_TYPE_RAIDZ);

		if (*p == '\0') {
			/* when unspecified default to single parity */
			return (1);
		} else if (*p == '0') {
			/* no zero prefixes allowed */
			return (0);
		} else {
			/* 0-3, no suffixes allowed */
			char *end;
			errno = 0;
			parity = strtol(p, &end, 10);
			if (errno != 0 || *end != '\0' ||
			    parity < 1 || parity > VDEV_RAIDZ_MAXPARITY) {
				return (0);
			}
		}
	} else if (strncmp(type, VDEV_TYPE_DRAID,
	    strlen(VDEV_TYPE_DRAID)) == 0) {
		p = type + strlen(VDEV_TYPE_DRAID);

		if (*p == '\0' || *p == ':') {
			/* when unspecified default to single parity */
			return (1);
		} else if (*p == '0') {
			/* no zero prefixes allowed */
			return (0);
		} else {
			/* 0-3, allowed suffixes: '\0' or ':' */
			char *end;
			errno = 0;
			parity = strtol(p, &end, 10);
			if (errno != 0 ||
			    parity < 1 || parity > VDEV_DRAID_MAXPARITY ||
			    (*end != '\0' && *end != ':')) {
				return (0);
			}
		}
	}

	return ((int)parity);
}

/*
 * Assign the minimum and maximum number of devices allowed for
 * the specified type.  On error NULL is returned, otherwise the
 * type prefix is returned (raidz, mirror, etc).
 */
static const char *
is_grouping(const char *type, int *mindev, int *maxdev)
{
	int nparity;

	if (strncmp(type, VDEV_TYPE_RAIDZ, strlen(VDEV_TYPE_RAIDZ)) == 0 ||
	    strncmp(type, VDEV_TYPE_DRAID, strlen(VDEV_TYPE_DRAID)) == 0) {
		nparity = get_parity(type);
		if (nparity == 0)
			return (NULL);
		if (mindev != NULL)
			*mindev = nparity + 1;
		if (maxdev != NULL)
			*maxdev = 255;

		if (strncmp(type, VDEV_TYPE_RAIDZ,
		    strlen(VDEV_TYPE_RAIDZ)) == 0) {
			return (VDEV_TYPE_RAIDZ);
		} else {
			return (VDEV_TYPE_DRAID);
		}
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

	if (strcmp(type, VDEV_ALLOC_BIAS_SPECIAL) == 0 ||
	    strcmp(type, VDEV_ALLOC_BIAS_DEDUP) == 0) {
		if (mindev != NULL)
			*mindev = 1;
		return (type);
	}

	if (strcmp(type, "cache") == 0) {
		if (mindev != NULL)
			*mindev = 1;
		return (VDEV_TYPE_L2CACHE);
	}

	return (NULL);
}

/*
 * Extract the configuration parameters encoded in the dRAID type and
 * use them to generate a dRAID configuration.  The expected format is:
 *
 * draid[<parity>][:<data><d|D>][:<children><c|C>][:<spares><s|S>]
 *
 * The intent is to be able to generate a good configuration when no
 * additional information is provided.  The only mandatory component
 * of the 'type' is the 'draid' prefix.  If a value is not provided
 * then reasonable defaults are used.  The optional components may
 * appear in any order but the d/s/c suffix is required.
 *
 * Valid inputs:
 * - data:     number of data devices per group (1-255)
 * - parity:   number of parity blocks per group (1-3)
 * - spares:   number of distributed spare (0-100)
 * - children: total number of devices (1-255)
 *
 * Examples:
 * - zpool create tank draid <devices...>
 * - zpool create tank draid2:8d:51c:2s <devices...>
 */
static int
draid_config_by_type(nvlist_t *nv, const char *type, uint64_t children)
{
	uint64_t nparity = 1;
	uint64_t nspares = 0;
	uint64_t ndata = UINT64_MAX;
	uint64_t ngroups = 1;
	long value;

	if (strncmp(type, VDEV_TYPE_DRAID, strlen(VDEV_TYPE_DRAID)) != 0)
		return (EINVAL);

	nparity = (uint64_t)get_parity(type);
	if (nparity == 0 || nparity > VDEV_DRAID_MAXPARITY) {
		fprintf(stderr,
		    gettext("invalid dRAID parity level %llu; must be "
		    "between 1 and %d\n"), (u_longlong_t)nparity,
		    VDEV_DRAID_MAXPARITY);
		return (EINVAL);
	}

	char *p = (char *)type;
	while ((p = strchr(p, ':')) != NULL) {
		char *end;

		p = p + 1;
		errno = 0;

		if (!isdigit(p[0])) {
			(void) fprintf(stderr, gettext("invalid dRAID "
			    "syntax; expected [:<number><c|d|s>] not '%s'\n"),
			    type);
			return (EINVAL);
		}

		/* Expected non-zero value with c/d/s suffix */
		value = strtol(p, &end, 10);
		char suffix = tolower(*end);
		if (errno != 0 ||
		    (suffix != 'c' && suffix != 'd' && suffix != 's')) {
			(void) fprintf(stderr, gettext("invalid dRAID "
			    "syntax; expected [:<number><c|d|s>] not '%s'\n"),
			    type);
			return (EINVAL);
		}

		if (suffix == 'c') {
			if ((uint64_t)value != children) {
				fprintf(stderr,
				    gettext("invalid number of dRAID children; "
				    "%llu required but %llu provided\n"),
				    (u_longlong_t)value,
				    (u_longlong_t)children);
				return (EINVAL);
			}
		} else if (suffix == 'd') {
			ndata = (uint64_t)value;
		} else if (suffix == 's') {
			nspares = (uint64_t)value;
		} else {
			verify(0); /* Unreachable */
		}
	}

	/*
	 * When a specific number of data disks is not provided limit a
	 * redundancy group to 8 data disks.  This value was selected to
	 * provide a reasonable tradeoff between capacity and performance.
	 */
	if (ndata == UINT64_MAX) {
		if (children > nspares + nparity) {
			ndata = MIN(children - nspares - nparity, 8);
		} else {
			fprintf(stderr, gettext("request number of "
			    "distributed spares %llu and parity level %llu\n"
			    "leaves no disks available for data\n"),
			    (u_longlong_t)nspares, (u_longlong_t)nparity);
			return (EINVAL);
		}
	}

	/* Verify the maximum allowed group size is never exceeded. */
	if (ndata == 0 || (ndata + nparity > children - nspares)) {
		fprintf(stderr, gettext("requested number of dRAID data "
		    "disks per group %llu is too high,\nat most %llu disks "
		    "are available for data\n"), (u_longlong_t)ndata,
		    (u_longlong_t)(children - nspares - nparity));
		return (EINVAL);
	}

	/*
	 * Verify the requested number of spares can be satisfied.
	 * An arbitrary limit of 100 distributed spares is applied.
	 */
	if (nspares > 100 || nspares > (children - (ndata + nparity))) {
		fprintf(stderr,
		    gettext("invalid number of dRAID spares %llu; additional "
		    "disks would be required\n"), (u_longlong_t)nspares);
		return (EINVAL);
	}

	/* Verify the requested number children is sufficient. */
	if (children < (ndata + nparity + nspares)) {
		fprintf(stderr, gettext("%llu disks were provided, but at "
		    "least %llu disks are required for this config\n"),
		    (u_longlong_t)children,
		    (u_longlong_t)(ndata + nparity + nspares));
	}

	if (children > VDEV_DRAID_MAX_CHILDREN) {
		fprintf(stderr, gettext("%llu disks were provided, but "
		    "dRAID only supports up to %u disks"),
		    (u_longlong_t)children, VDEV_DRAID_MAX_CHILDREN);
	}

	/*
	 * Calculate the minimum number of groups required to fill a slice.
	 * This is the LCM of the stripe width (ndata + nparity) and the
	 * number of data drives (children - nspares).
	 */
	while (ngroups * (ndata + nparity) % (children - nspares) != 0)
		ngroups++;

	/* Store the basic dRAID configuration. */
	fnvlist_add_uint64(nv, ZPOOL_CONFIG_NPARITY, nparity);
	fnvlist_add_uint64(nv, ZPOOL_CONFIG_DRAID_NDATA, ndata);
	fnvlist_add_uint64(nv, ZPOOL_CONFIG_DRAID_NSPARES, nspares);
	fnvlist_add_uint64(nv, ZPOOL_CONFIG_DRAID_NGROUPS, ngroups);

	return (0);
}

/*
 * Construct a syntactically valid vdev specification,
 * and ensure that all devices and files exist and can be opened.
 * Note: we don't bother freeing anything in the error paths
 * because the program is just going to exit anyway.
 */
static nvlist_t *
construct_spec(nvlist_t *props, int argc, char **argv)
{
	nvlist_t *nvroot, *nv, **top, **spares, **l2cache;
	int t, toplevels, mindev, maxdev, nspares, nlogs, nl2cache;
	const char *type, *fulltype;
	boolean_t is_log, is_special, is_dedup, is_spare;
	boolean_t seen_logs;

	top = NULL;
	toplevels = 0;
	spares = NULL;
	l2cache = NULL;
	nspares = 0;
	nlogs = 0;
	nl2cache = 0;
	is_log = is_special = is_dedup = is_spare = B_FALSE;
	seen_logs = B_FALSE;
	nvroot = NULL;

	while (argc > 0) {
		fulltype = argv[0];
		nv = NULL;

		/*
		 * If it's a mirror, raidz, or draid the subsequent arguments
		 * are its leaves -- until we encounter the next mirror,
		 * raidz or draid.
		 */
		if ((type = is_grouping(fulltype, &mindev, &maxdev)) != NULL) {
			nvlist_t **child = NULL;
			int c, children = 0;

			if (strcmp(type, VDEV_TYPE_SPARE) == 0) {
				if (spares != NULL) {
					(void) fprintf(stderr,
					    gettext("invalid vdev "
					    "specification: 'spare' can be "
					    "specified only once\n"));
					goto spec_out;
				}
				is_spare = B_TRUE;
				is_log = is_special = is_dedup = B_FALSE;
			}

			if (strcmp(type, VDEV_TYPE_LOG) == 0) {
				if (seen_logs) {
					(void) fprintf(stderr,
					    gettext("invalid vdev "
					    "specification: 'log' can be "
					    "specified only once\n"));
					goto spec_out;
				}
				seen_logs = B_TRUE;
				is_log = B_TRUE;
				is_special = is_dedup = is_spare = B_FALSE;
				argc--;
				argv++;
				/*
				 * A log is not a real grouping device.
				 * We just set is_log and continue.
				 */
				continue;
			}

			if (strcmp(type, VDEV_ALLOC_BIAS_SPECIAL) == 0) {
				is_special = B_TRUE;
				is_log = is_dedup = is_spare = B_FALSE;
				argc--;
				argv++;
				continue;
			}

			if (strcmp(type, VDEV_ALLOC_BIAS_DEDUP) == 0) {
				is_dedup = B_TRUE;
				is_log = is_special = is_spare = B_FALSE;
				argc--;
				argv++;
				continue;
			}

			if (strcmp(type, VDEV_TYPE_L2CACHE) == 0) {
				if (l2cache != NULL) {
					(void) fprintf(stderr,
					    gettext("invalid vdev "
					    "specification: 'cache' can be "
					    "specified only once\n"));
					goto spec_out;
				}
				is_log = is_special = B_FALSE;
				is_dedup = is_spare = B_FALSE;
			}

			if (is_log || is_special || is_dedup) {
				if (strcmp(type, VDEV_TYPE_MIRROR) != 0) {
					(void) fprintf(stderr,
					    gettext("invalid vdev "
					    "specification: unsupported '%s' "
					    "device: %s\n"), is_log ? "log" :
					    "special", type);
					goto spec_out;
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
				if ((nv = make_leaf_vdev(props, argv[c],
				    !(is_log || is_special || is_dedup ||
				    is_spare))) == NULL) {
					for (c = 0; c < children - 1; c++)
						nvlist_free(child[c]);
					free(child);
					goto spec_out;
				}

				child[children - 1] = nv;
			}

			if (children < mindev) {
				(void) fprintf(stderr, gettext("invalid vdev "
				    "specification: %s requires at least %d "
				    "devices\n"), argv[0], mindev);
				for (c = 0; c < children; c++)
					nvlist_free(child[c]);
				free(child);
				goto spec_out;
			}

			if (children > maxdev) {
				(void) fprintf(stderr, gettext("invalid vdev "
				    "specification: %s supports no more than "
				    "%d devices\n"), argv[0], maxdev);
				for (c = 0; c < children; c++)
					nvlist_free(child[c]);
				free(child);
				goto spec_out;
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
				/* create a top-level vdev with children */
				verify(nvlist_alloc(&nv, NV_UNIQUE_NAME,
				    0) == 0);
				verify(nvlist_add_string(nv, ZPOOL_CONFIG_TYPE,
				    type) == 0);
				verify(nvlist_add_uint64(nv,
				    ZPOOL_CONFIG_IS_LOG, is_log) == 0);
				if (is_log) {
					verify(nvlist_add_string(nv,
					    ZPOOL_CONFIG_ALLOCATION_BIAS,
					    VDEV_ALLOC_BIAS_LOG) == 0);
				}
				if (is_special) {
					verify(nvlist_add_string(nv,
					    ZPOOL_CONFIG_ALLOCATION_BIAS,
					    VDEV_ALLOC_BIAS_SPECIAL) == 0);
				}
				if (is_dedup) {
					verify(nvlist_add_string(nv,
					    ZPOOL_CONFIG_ALLOCATION_BIAS,
					    VDEV_ALLOC_BIAS_DEDUP) == 0);
				}
				if (strcmp(type, VDEV_TYPE_RAIDZ) == 0) {
					verify(nvlist_add_uint64(nv,
					    ZPOOL_CONFIG_NPARITY,
					    mindev - 1) == 0);
				}
				if (strcmp(type, VDEV_TYPE_DRAID) == 0) {
					if (draid_config_by_type(nv,
					    fulltype, children) != 0) {
						for (c = 0; c < children; c++)
							nvlist_free(child[c]);
						free(child);
						goto spec_out;
					}
				}
				verify(nvlist_add_nvlist_array(nv,
				    ZPOOL_CONFIG_CHILDREN,
				    (const nvlist_t **)child, children) == 0);

				for (c = 0; c < children; c++)
					nvlist_free(child[c]);
				free(child);
			}
		} else {
			/*
			 * We have a device.  Pass off to make_leaf_vdev() to
			 * construct the appropriate nvlist describing the vdev.
			 */
			if ((nv = make_leaf_vdev(props, argv[0], !(is_log ||
			    is_special || is_dedup || is_spare))) == NULL)
				goto spec_out;

			verify(nvlist_add_uint64(nv,
			    ZPOOL_CONFIG_IS_LOG, is_log) == 0);
			if (is_log) {
				verify(nvlist_add_string(nv,
				    ZPOOL_CONFIG_ALLOCATION_BIAS,
				    VDEV_ALLOC_BIAS_LOG) == 0);
				nlogs++;
			}

			if (is_special) {
				verify(nvlist_add_string(nv,
				    ZPOOL_CONFIG_ALLOCATION_BIAS,
				    VDEV_ALLOC_BIAS_SPECIAL) == 0);
			}
			if (is_dedup) {
				verify(nvlist_add_string(nv,
				    ZPOOL_CONFIG_ALLOCATION_BIAS,
				    VDEV_ALLOC_BIAS_DEDUP) == 0);
			}
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
		goto spec_out;
	}

	if (seen_logs && nlogs == 0) {
		(void) fprintf(stderr, gettext("invalid vdev specification: "
		    "log requires at least 1 device\n"));
		goto spec_out;
	}

	/*
	 * Finally, create nvroot and add all top-level vdevs to it.
	 */
	verify(nvlist_alloc(&nvroot, NV_UNIQUE_NAME, 0) == 0);
	verify(nvlist_add_string(nvroot, ZPOOL_CONFIG_TYPE,
	    VDEV_TYPE_ROOT) == 0);
	verify(nvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    (const nvlist_t **)top, toplevels) == 0);
	if (nspares != 0)
		verify(nvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
		    (const nvlist_t **)spares, nspares) == 0);
	if (nl2cache != 0)
		verify(nvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
		    (const nvlist_t **)l2cache, nl2cache) == 0);

spec_out:
	for (t = 0; t < toplevels; t++)
		nvlist_free(top[t]);
	for (t = 0; t < nspares; t++)
		nvlist_free(spares[t]);
	for (t = 0; t < nl2cache; t++)
		nvlist_free(l2cache[t]);

	free(spares);
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

		if (!flags.dryrun && make_disks(zhp, newroot, B_FALSE) != 0) {
			nvlist_free(newroot);
			return (NULL);
		}

		/* avoid any tricks in the spec */
		verify(nvlist_lookup_nvlist_array(newroot,
		    ZPOOL_CONFIG_CHILDREN, &child, &children) == 0);
		for (c = 0; c < children; c++) {
			const char *path;
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
		nvlist_free(newroot);
		return (NULL);
	}

	return (newroot);
}

static int
num_normal_vdevs(nvlist_t *nvroot)
{
	nvlist_t **top;
	uint_t t, toplevels, normal = 0;

	verify(nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    &top, &toplevels) == 0);

	for (t = 0; t < toplevels; t++) {
		uint64_t log = B_FALSE;

		(void) nvlist_lookup_uint64(top[t], ZPOOL_CONFIG_IS_LOG, &log);
		if (log)
			continue;
		if (nvlist_exists(top[t], ZPOOL_CONFIG_ALLOCATION_BIAS))
			continue;

		normal++;
	}

	return (normal);
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

	if (zhp && ((poolconfig = zpool_get_config(zhp, NULL)) == NULL)) {
		nvlist_free(newroot);
		return (NULL);
	}

	/*
	 * Validate each device to make sure that it's not shared with another
	 * subsystem.  We do this even if 'force' is set, because there are some
	 * uses (such as a dedicated dump device) that even '-f' cannot
	 * override.
	 */
	if (is_device_in_use(poolconfig, newroot, force, replacing, B_FALSE)) {
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
	 * On pool create the new vdev spec must have one normal vdev.
	 */
	if (poolconfig == NULL && num_normal_vdevs(newroot) == 0) {
		vdev_error(gettext("at least one general top-level vdev must "
		    "be specified\n"));
		nvlist_free(newroot);
		return (NULL);
	}

	/*
	 * Run through the vdev specification and label any whole disks found.
	 */
	if (!dryrun && make_disks(zhp, newroot, replacing) != 0) {
		nvlist_free(newroot);
		return (NULL);
	}

	return (newroot);
}
