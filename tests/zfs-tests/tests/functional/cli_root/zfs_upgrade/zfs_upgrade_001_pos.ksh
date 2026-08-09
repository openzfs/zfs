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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_upgrade/zfs_upgrade.kshlib

#
# DESCRIPTION:
#	Executing 'zfs upgrade' command succeeds, it should report
#	the current system version and list all old-version filesystems.
#	If no old-version filesystems be founded, it prints out
#	"All filesystems are formatted with the current version."
#
# STRATEGY:
# 1. Prepare a set of datasets which contain old-version and current version.
# 2. Execute 'zfs upgrade', verify return 0, and it prints out
#	the current system version and list all old-version filesystems.
# 3. Remove all old-version filesystems, then execute 'zfs upgrade' again,
#	verify return 0, and get the expected message.
#

verify_runnable "both"

function cleanup
{
	datasetexists $rootfs && destroy_dataset $rootfs -Rf
	log_must zfs create $rootfs

	for file in $output $oldoutput ; do
		if [[ -f $file ]]; then
			log_must rm -f $file
		fi
	done
}

log_assert "Executing 'zfs upgrade' command succeeds."
log_onexit cleanup

rootfs=$TESTPOOL/$TESTFS
typeset output=$TEST_BASE_DIR/zfs-versions.$$
typeset oldoutput=$TEST_BASE_DIR/zfs-versions-old.$$
typeset expect_str1="This system is currently running ZFS filesystem version"
typeset expect_str2="All filesystems are formatted with the current version"
typeset expect_str3="The following filesystems are out of date, and can be upgraded"
typeset -i COUNT OLDCOUNT

zfs upgrade | awk '$1 ~ "^[0-9]+$" {print $2}'> $oldoutput
OLDCOUNT=$(wc -l < $oldoutput)

old_datasets=""
for version in $ZFS_ALL_VERSIONS ; do
	typeset verfs
	eval verfs=\$ZFS_VERSION_$version
	typeset current_fs=$rootfs/$verfs
	typeset current_snap=${current_fs}@snap
	typeset current_clone=$rootfs/clone$verfs
	log_must zfs create -o version=${version} ${current_fs}
	log_must zfs snapshot ${current_snap}
	log_must zfs clone ${current_snap} ${current_clone}

	if (( version != $ZFS_VERSION )); then
		old_datasets="$old_datasets ${current_fs} ${current_clone}"
	fi
done

if is_global_zone; then
	log_must zfs create -V 100m $rootfs/$TESTVOL
fi

log_must eval 'zfs upgrade > $output 2>&1'

# we also check that the usage message contains at least a description
# of the current ZFS version.
log_must grep -q "${expect_str1} $ZFS_VERSION" $output
zfs upgrade | awk '$1 ~ "^[0-9]+$" {print $2}'> $output
COUNT=$(wc -l < $output)

typeset -i i=0
for fs in ${old_datasets}; do
	log_must grep "^$fs$" $output
	(( i = i + 1 ))
done

if (( i != COUNT - OLDCOUNT )); then
	cat $output
	log_fail "More old-version filesystems print out than expect."
fi

for fs in $old_datasets ; do
	datasetexists $fs && destroy_dataset $fs -Rf
done

log_must eval 'zfs upgrade > $output 2>&1'
log_must grep -q "${expect_str1} $ZFS_VERSION" $output
if (( OLDCOUNT == 0 )); then
	log_must grep -q "${expect_str2}" $output
else
	log_must grep -q "${expect_str3}" $output
fi
zfs upgrade | awk '$1 ~ "^[0-9]+$" {print $2}'> $output
COUNT=$(wc -l < $output)

if (( COUNT != OLDCOUNT )); then
	cat $output
	log_fail "Unexpected old-version filesystems print out."
fi

log_pass "Executing 'zfs upgrade' command succeeds."
