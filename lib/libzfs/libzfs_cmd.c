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
 * Copyright (c) 2011, 2018 by Delphix. All rights reserved.
 * Copyright 2012 Milan Jurik. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 * Copyright (c) 2013 Steven Hartland.  All rights reserved.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>.
 * Copyright 2016 Nexenta Systems, Inc.
 * Copyright (c) 2018 Datto Inc.
 * Copyright (c) 2018 John Ramsden.
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <libintl.h>
#include <libuutil.h>
#include <libnvpair.h>
#include <locale.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <zone.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <sys/debug.h>
#include <sys/list.h>
#include <sys/mkdev.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/fs/zfs.h>
#include <sys/systeminfo.h>
#include <sys/types.h>
#include <time.h>
#include <sys/zfs_project.h>
#include <libzfs_core.h>
#include <zfs_prop.h>
#include <zfs_deleg.h>
#include <libuutil.h>

#include "libzfs.h"
#include "libzfs_impl.h"
#include "zfs_comutil.h"

static libzfs_handle_t *g_zfs;

static FILE *mnttab_file;
static char history_str[HIS_MAX_RECORD_LEN];
static boolean_t log_history = B_TRUE;

/*
 * Utility function to guarantee malloc() success.
 */

static int
update_zfs_cmd_data(zfs_cmd_data_t *cmd_data)
{
	if (cmd_data != NULL) {
		mnttab_file = *cmd_data->mnttab_file;
		g_zfs = *cmd_data->g_zfs;
		snprintf(history_str, HIS_MAX_RECORD_LEN, "%s",
		    cmd_data->history_str);
		log_history = *cmd_data->log_history;
	}

	return (0);
}

static int
zfs_mount_and_share(libzfs_handle_t *hdl, const char *dataset, zfs_type_t type)
{
	zfs_handle_t *zhp = NULL;
	int ret = 0;

	zhp = zfs_open(hdl, dataset, type);
	if (zhp == NULL)
		return (1);

	/*
	 * Volumes may neither be mounted or shared.  Potentially in the
	 * future filesystems detected on these volumes could be mounted.
	 */
	if (zfs_get_type(zhp) == ZFS_TYPE_VOLUME) {
		zfs_close(zhp);
		return (0);
	}

	/*
	 * Mount and/or share the new filesystem as appropriate.  We provide a
	 * verbose error message to let the user know that their filesystem was
	 * in fact created, even if we failed to mount or share it.
	 *
	 * If the user doesn't want the dataset automatically mounted, then
	 * skip the mount/share step
	 */
	if (zfs_prop_valid_for_type(ZFS_PROP_CANMOUNT, type, B_FALSE) &&
	    zfs_prop_get_int(zhp, ZFS_PROP_CANMOUNT) == ZFS_CANMOUNT_ON) {
		if (geteuid() != 0) {
			fprintf(stre(), gettext(
			    "filesystem successfully created, but it "
			    "may only be mounted by root\n"));
			ret = 1;
		} else if (zfs_mount(zhp, NULL, 0) != 0) {
			fprintf(stre(), gettext(
			    "filesystem successfully created, "
			    "but not mounted\n"));
			ret = 1;
		} else if (zfs_share(zhp) != 0) {
			fprintf(stre(), gettext(
			    "filesystem successfully created, "
			    "but not shared\n"));
			ret = 1;
		}
	}

	zfs_close(zhp);

	return (ret);
}

/*
 * Given an existing dataset, create a writable copy whose initial contents
 * are the same as the source.  The newly created dataset maintains a
 * dependency on the original; the original cannot be destroyed so long as
 * the clone exists.
 *
 * The 'parents' boolean creates all the non-existing ancestors of the target
 * first.
 */
int
libzfs_cmd_zfs_clone(int argc, char **argv, nvlist_t *props,
    zfs_clone_options_t *options, zfs_cmd_data_t *cmd_data)
{
	update_zfs_cmd_data(cmd_data);

	zfs_handle_t *zhp = NULL;
	int ret = 0;

	/* check number of arguments */
	if (argc < 1) {
		(void) fprintf(stre(),
		    gettext("missing source dataset argument\n"));
		return (ZFS_CMD_PRINT_USAGE);
	}
	if (argc < 2) {
		(void) fprintf(stre(),
		    gettext("missing target dataset argument\n"));
		return (ZFS_CMD_PRINT_USAGE);
	}
	if (argc > 2) {
		(void) fprintf(stre(),
		    gettext("too many arguments\n"));
		return (ZFS_CMD_PRINT_USAGE);
	}

	char *source = argv[0];
	char *target = argv[1];

	if ((source == NULL) || (target == NULL)) {
		return (1);
	}

	/* open the source dataset */
	if ((zhp = zfs_open(g_zfs, source, ZFS_TYPE_SNAPSHOT)) == NULL) {
		return (1);
	}

	if (options->parents && zfs_name_valid(target,
	    (ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME))) {
		/*
		 * Now create the ancestors of the target dataset.  If the
		 * target already exists and '-p' option was used we should not
		 * complain.
		 */
		if (zfs_dataset_exists(g_zfs, target,
		    (ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME))) {
			zfs_close(zhp);
			return (0);
		}
		if (zfs_create_ancestors(g_zfs, target) != 0) {
			zfs_close(zhp);
			return (1);
		}
	}

	/* pass to libzfs */
	ret = zfs_clone(zhp, target, props);

	/* create the mountpoint if necessary */
	if (ret == 0) {
		if (log_history) {
			(void) zpool_log_history(g_zfs, history_str);
			log_history = B_FALSE;
		}

		ret = zfs_mount_and_share(g_zfs, target, ZFS_TYPE_DATASET);
	}

	zfs_close(zhp);

	return (!!ret);
}

/* END */
