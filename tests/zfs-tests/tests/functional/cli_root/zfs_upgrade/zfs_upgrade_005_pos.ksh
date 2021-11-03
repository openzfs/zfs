#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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
#	Executing 'zfs upgrade [-V version] -a' command succeeds,
#	it upgrade all filesystems to specific or current version.
#
# STRATEGY:
# 1. Prepare a set of datasets which contain old-version and current version.
# 2. Execute 'zfs upgrade [-V version] -a', verify return 0,
# 3. Verify all the filesystems be updated as expected.
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

		for subversion in $ZFS_ALL_VERSIONS ; do
			typeset subverfs
			eval subverfs=\$ZFS_VERSION_$subversion
			log_must zfs create -o version=${subversion} \
				${current_fs}/$subverfs
		done
		datasets="$datasets ${current_fs}"
	done
}

log_assert "Executing 'zfs upgrade [-V version] -a' command succeeds."
log_onexit cleanup

rootfs=$TESTPOOL/$TESTFS

typeset datasets

typeset newv
for newv in "" "current" $ZFS_VERSION; do
	setup_datasets
	if [[ -n $newv ]]; then
		opt="-V $newv"
		if [[ $newv == current ]]; then
			newv=$ZFS_VERSION
		fi
	else
		newv=$ZFS_VERSION
	fi

	export __ZFS_POOL_RESTRICT="$TESTPOOL"
	log_must zfs upgrade $opt -a
	unset __ZFS_POOL_RESTRICT

	for fs in $(zfs list -rH -t filesystem -o name $rootfs) ; do
		log_must check_fs_version $fs $newv
	done
	cleanup
done

log_pass "Executing 'zfs upgrade [-V version] -a' command succeeds."
