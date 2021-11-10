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
