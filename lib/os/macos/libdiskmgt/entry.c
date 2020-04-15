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
 * Copyright (c) 2016, Brendon Humphrey (brendon.humphrey@mac.com).
 */

#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <libintl.h>
#include <locale.h>
#include <sys/debug.h>
#include <strings.h>

#include <libnvpair.h>
#include <libdiskmgt.h>
#include "disks_private.h"

#define	ANY_ZPOOL_USE(who)			\
	(((who) == DM_WHO_ZPOOL_FORCE) ||	\
	((who) == DM_WHO_ZPOOL) ||		\
	((who) == DM_WHO_ZPOOL_SPARE))

void dm_get_slice_stats(char *slice, nvlist_t **dev_stats, int *errp);
nvlist_t *dm_get_stats(char *slice, int stat_type, int *errp);
static int build_usage_string(char *dname, char *by, char *data, char **msg,
int *found, int *errp);
void dm_get_usage_string(char *what, char *how, char **usage_string);


void
libdiskmgt_add_str(nvlist_t *attrs, const char *name, const char *val,
    int *errp)
{
	if (*errp == 0) {
		*errp = nvlist_add_string(attrs, name, val);
	}
}

/*
 * Returns 'in use' details, if found, about a specific dev_name,
 * based on the caller(who). It is important to note that it is possible
 * for there to be more than one 'in use' statistic regarding a dev_name.
 * The **msg parameter returns a list of 'in use' details. This message
 * is formatted via gettext().
 */
int
dm_inuse(char *dev_name, char **msg, dm_who_type_t who, int *errp)
{
	nvlist_t *dev_stats = NULL;
	char *by, *data;
	nvpair_t *nvwhat = NULL;
	nvpair_t *nvdesc = NULL;
	int	found = 0;

	*errp = 0;
	*msg = NULL;

	/*
	 * If the user doesn't want to do in use checking, return.
	 */

	if (NOINUSE_SET)
		return (0);

	dm_get_slice_stats(dev_name, &dev_stats, errp);
	if (dev_stats == NULL) {
		/*
		 * If there is an error, but it isn't a no device found error
		 * return the error as recorded. Otherwise, with a full
		 * block name, we might not be able to get the slice
		 * associated, and will get an ENODEV error. For example,
		 * an SVM metadevice will return a value from getfullblkname()
		 * but libdiskmgt won't be able to find this device for
		 * statistics gathering. This is expected and we should not
		 * report errnoneous errors.
		 */
		if (*errp) {
			if (*errp == ENODEV) {
				*errp = 0;
			}
		}
		//    free(dname);
		return (found);
	}

	for (;;) {

		nvwhat = nvlist_next_nvpair(dev_stats, nvdesc);
		nvdesc = nvlist_next_nvpair(dev_stats, nvwhat);

		/*
		 * End of the list found.
		 */
		if (nvwhat == NULL || nvdesc == NULL) {
			break;
		}
		/*
		 * Otherwise, we check to see if this client(who) cares
		 * about this in use scenario
		 */

		ASSERT(strcmp(nvpair_name(nvwhat), DM_USED_BY) == 0);
		ASSERT(strcmp(nvpair_name(nvdesc), DM_USED_NAME) == 0);
		/*
		 * If we error getting the string value continue on
		 * to the next pair(if there is one)
		 */
		if (nvpair_value_string(nvwhat, &by)) {
			continue;
		}
		if (nvpair_value_string(nvdesc, &data)) {
			continue;
		}

		switch (who) {

		case DM_WHO_ZPOOL_FORCE:
			if (strcmp(by, DM_USE_FS) == 0 ||
			    strcmp(by, DM_USE_EXPORTED_ZPOOL) == 0 ||
			    strcmp(by,  DM_USE_OS_PARTITION) == 0)
				break;
			/* FALLTHROUGH */
			zfs_fallthrough;
		case DM_WHO_ZPOOL:
			if (build_usage_string(dev_name,
			    by, data, msg, &found, errp) != 0) {
				if (*errp)
					goto out;
			}
			break;

		case DM_WHO_ZPOOL_SPARE:
			if (strcmp(by, DM_USE_SPARE_ZPOOL) != 0) {
				if (build_usage_string(dev_name, by,
				    data, msg, &found, errp) != 0) {
					if (*errp)
						goto out;
				}
			}
			break;

		default:
			/*
			 * nothing found in use for this client
			 * of libdiskmgt. Default is 'not in use'.
			 */
			break;
		}
	}
out:
	nvlist_free(dev_stats);

	return (found);
}

nvlist_t *
dm_get_stats(char *slice, int stat_type, int *errp)
{
	nvlist_t	*stats = NULL;

	/* BGH - removed everything except ability to check a slice */

	if (stat_type == DM_SLICE_STAT_USE) {
		/*
		 * If NOINUSE_CHECK is set, we do not perform
		 * the in use checking if the user has set stat_type
		 * DM_SLICE_STAT_USE
		 */
		if (NOINUSE_SET) {
			stats = NULL;
			return (stats);
		}
	}
	stats = slice_get_stats(slice, stat_type, errp);

	return (stats);
}


/*
 * Convenience function to get slice stats. This is where we are going to
 * depart from the illumos implementation - libdiskmgt on that
 * platform has a lot more tricks that are not applicable
 * to O3X.
 */
void
dm_get_slice_stats(char *slice, nvlist_t **dev_stats, int *errp)
{
	*dev_stats = NULL;
	*errp = 0;

	if (slice == NULL) {
		return;
	}

	*dev_stats = dm_get_stats(slice, DM_SLICE_STAT_USE, errp);
}

void
dm_get_usage_string(char *what, char *how, char **usage_string)
{
	if (usage_string == NULL || what == NULL) {
		return;
	}
	*usage_string = NULL;

	if (strcmp(what, DM_USE_MOUNT) == 0) {
		if (strcmp(how, "swap") == 0) {
			*usage_string = dgettext(TEXT_DOMAIN,
			    "%s is currently used by swap. Please see swap(1M)."
			    "\n");
		} else {
			*usage_string = dgettext(TEXT_DOMAIN,
			    "%s is currently mounted on %s."
			    " Please see umount(1M).\n");
		}
	} else if (strcmp(what, DM_USE_FS) == 0 ||
	    strcmp(what, DM_USE_FS_NO_FORCE) == 0) {
		*usage_string = dgettext(TEXT_DOMAIN,
		    "%s contains a %s filesystem.\n");
	} else if (strcmp(what, DM_USE_EXPORTED_ZPOOL) == 0) {
		*usage_string = dgettext(TEXT_DOMAIN,
		    "%s is part of exported or potentially active ZFS pool %s. "
		    "Please see zpool(1M).\n");
	} else if (strcmp(what, DM_USE_ACTIVE_ZPOOL) == 0) {
		*usage_string = dgettext(TEXT_DOMAIN,
		    "%s is part of active ZFS pool %s. Please see zpool(1M)."
		    "\n");
	} else if (strcmp(what, DM_USE_SPARE_ZPOOL) == 0) {
		*usage_string = dgettext(TEXT_DOMAIN,
		    "%s is reserved as a hot spare for ZFS pool %s.  Please "
		    "see zpool(1M).\n");
	} else if (strcmp(what, DM_USE_L2CACHE_ZPOOL) == 0) {
		*usage_string = dgettext(TEXT_DOMAIN,
		    "%s is in use as a cache device for ZFS pool %s.  "
		    "Please see zpool(1M).\n");
	} else if (strcmp(what, DM_USE_CORESTORAGE_PV) == 0) {
		*usage_string = dgettext(TEXT_DOMAIN,
		    "%s is in use as a corestorage physical volume.  "
		    "Please see diskutil(8).\n");
	} else if (strcmp(what, DM_USE_CORESTORAGE_LOCKED_LV) == 0) {
		*usage_string = dgettext(TEXT_DOMAIN,
		    "%s is a corestorage logical volume, "
		    "but cannot be used as it is locked.  "
		    "Please see diskutil(8).\n");
	} else if (strcmp(what, DM_USE_CORESTORAGE_CONVERTING_LV) == 0) {
		*usage_string = dgettext(TEXT_DOMAIN,
		    "%s is a corestorage physical volume, but is still "
		    "converting (%s).\n"
		    "Creating a zpool while converting will result in "
		    "data corruption.\n"
		    "Please see diskutil(8).\n");
	} else if (strcmp(what, DM_USE_CORESTORAGE_OFFLINE_LV) == 0) {
		*usage_string = dgettext(TEXT_DOMAIN,
		    "%s is a corestorage physical volume, but is not "
		    "online (%s). Please see diskutil(8).\n");
	} else if (strcmp(what, DM_USE_OS_PARTITION) == 0 ||
	    strcmp(what, DM_USE_OS_PARTITION_NO_FORCE) == 0) {
		*usage_string = dgettext(TEXT_DOMAIN,
		    "%s is a %s partition. "
		    "Please see diskutil(8).\n");
	}
}

/*
 * Build the usage string for the in use data. Return the build string in
 * the msg parameter. This function takes care of reallocing all the memory
 * for this usage string. Usage string is returned already formatted for
 * localization.
 */
static int
build_usage_string(char *dname, char *by, char *data, char **msg,
    int *found, int *errp)
{
	int	len0;
	int	len1;
	char	*use;
	char	*p;

	*errp = 0;

	dm_get_usage_string(by, data, &use);
	if (!use) {
		return (-1);
	}

	if (*msg)
		len0 = strlen(*msg);
	else
		len0 = 0;
	/* LINTED */
	len1 = snprintf(NULL, 0, use, dname, data);

	/*
	 * If multiple in use details they
	 * are listed 1 per line for ease of
	 * reading. dm_find_usage_string
	 * formats these appropriately.
	 */
	if ((p = realloc(*msg, len0 + len1 + 1)) == NULL) {
		*errp = errno;
		free(*msg);
		return (-1);
	}
	*msg = p;

	/* LINTED */
	(void) snprintf(*msg + len0, len1 + 1, use, dname, data);
	(*found)++;
	return (0);
}
