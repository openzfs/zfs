#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_upgrade/zfs_upgrade.kshlib

#
# DESCRIPTION:
#	Executing 'zfs upgrade [-V version] filesystem' command succeeds,
#	it could upgrade a filesystem to specific version or current version.
#
# STRATEGY:
# 1. Prepare a set of datasets which contain old-version and current version.
# 2. Execute 'zfs upgrade [-V version] filesystem', verify return 0,
# 3. Verify the filesystem be updated as expected.
#

verify_runnable "both"

function cleanup
{
	datasetexists $rootfs && destroy_dataset $rootfs -Rf
	log_must zfs create $rootfs
}

function setup_datasets
{
	datasets=""
	for version in $ZFS_ALL_VERSIONS ; do
		typeset verfs
		eval verfs=\$ZFS_VERSION_$version
		typeset current_fs=$rootfs/$verfs
		typeset current_snap=${current_fs}@snap
		typeset current_clone=$rootfs/clone$verfs
		log_must zfs create -o version=${version} ${current_fs}
		log_must zfs snapshot ${current_snap}
		log_must zfs clone ${current_snap} ${current_clone}
		datasets="$datasets ${current_fs} ${current_clone}"
	done
}

log_assert "Executing 'zfs upgrade [-V version] filesystem' command succeeds."
log_onexit cleanup

rootfs=$TESTPOOL/$TESTFS
typeset datasets

typeset newv
for newv in "" "current" $ZFS_ALL_VERSIONS; do
	setup_datasets
	for fs in $datasets ; do
		typeset -i oldv=$(get_prop version $fs)

		if [[ -n $newv ]]; then
			opt="-V $newv"
			if [[ $newv == current ]]; then
				newv=$ZFS_VERSION
			fi
		else
			newv=$ZFS_VERSION
		fi

		if (( newv >= oldv )); then
			log_must eval 'zfs upgrade $opt $fs > /dev/null 2>&1'
			log_must check_fs_version $fs $newv
		else
			log_mustnot eval 'zfs upgrade $opt $fs > /dev/null 2>&1'
			log_must check_fs_version $fs $oldv
		fi
	done
	cleanup
done

log_pass "Executing 'zfs upgrade [-V version] filesystem' command succeeds."
