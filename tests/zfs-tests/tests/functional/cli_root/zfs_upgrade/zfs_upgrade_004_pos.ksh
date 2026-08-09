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
#	Executing 'zfs upgrade -r [-V version] filesystem' command succeeds,
#	it upgrade filesystem recursively to specific or current version.
#
# STRATEGY:
# 1. Prepare a set of datasets which contain old-version and current version.
# 2. Execute 'zfs upgrade -r [-V version] filesystem', verify return 0,
# 3. Verify the filesystem be updated recursively as expected.
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

log_assert "Executing 'zfs upgrade -r [-V version] filesystem' command succeeds."
log_onexit cleanup

rootfs=$TESTPOOL/$TESTFS

typeset datasets

typeset newv
for newv in "" "current" $ZFS_VERSION; do
	setup_datasets
	for topfs in $datasets ; do
		if [[ -n $newv ]]; then
			opt="-V $newv"
			if [[ $newv == current ]]; then
				newv=$ZFS_VERSION
			fi
		else
			newv=$ZFS_VERSION
		fi

		log_must eval 'zfs upgrade -r $opt $topfs > /dev/null 2>&1'

		for fs in $(zfs list -rH -t filesystem -o name $topfs) ; do
			log_must check_fs_version $fs $newv
		done
	done
	cleanup
done

log_pass "Executing 'zfs upgrade -r [-V version] filesystem' command succeeds."
