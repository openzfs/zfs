#!/bin/ksh -p
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_mount/zfs_mount.kshlib

# DESCRIPTION:
#       Verify that 'zfs mount -a' succeeds given a set of filesystems
#       whose mountpoints have a parent/child relationship which is
#       counter to the filesystem parent/child relationship.
#
# STRATEGY:
#       1. Create zfs filesystems within the given pool.
#       2. Unmount all the filesystems.
#       3. Verify that 'zfs mount -a' command succeed,
#	   and all available ZFS filesystems are mounted.
#	4. Verify that 'zfs mount' is identical with 'df -F zfs'
#

verify_runnable "both"

typeset -a filesystems

function setup_all
{
	typeset path=${TEST_BASE_DIR%%/}/testroot$$/$TESTPOOL
	typeset fscount=10

	#
	# Generate an array of filesystem names that represent a deep
	# hierarchy as such:
	#
	# 0
	# 0/1
	# 0/1/2
	# 0/1/2/3
	# 0/1/2/3/4
	# ...
	#
	fs=0
	for ((i=0; i<$fscount; i++)); do
		if [[ $i -gt 0 ]]; then
			fs=$fs/$i
		fi
		filesystems+=($fs)
	done

	# Create all of the above filesystems
	for ((i=0; i<$fscount; i++)); do
		fs=${filesystems[$i]}
		setup_filesystem "$DISKS" "$TESTPOOL" "$fs" "$path/$i" ctr
	done

	zfs list -r $TESTPOOL

	#
	# Unmount all of the above so that we can setup our convoluted
	# mount paths.
	#
	export __ZFS_POOL_RESTRICT="$TESTPOOL"
	log_must zfs $unmountall
	unset __ZFS_POOL_RESTRICT

	#
	# Configure the mount paths so that each mountpoint is contained
	# in a child filesystem. We should end up with something like the
	# following structure (modulo the number of filesystems):
	#
	# NAME                       MOUNTPOINT
	# testpool                   /testpool
	# testpool/0                 /testroot25416/testpool/0/1/2/3/4/5/6
	# testpool/0/1               /testroot25416/testpool/0/1/2/3/4/5
	# testpool/0/1/2             /testroot25416/testpool/0/1/2/3/4
	# testpool/0/1/2/3           /testroot25416/testpool/0/1/2/3
	# testpool/0/1/2/3/4         /testroot25416/testpool/0/1/2
	# testpool/0/1/2/3/4/5       /testroot25416/testpool/0/1
	# testpool/0/1/2/3/4/5/6     /testroot25416/testpool/0
	#
	for ((i=0; i<$fscount; i++)); do
		fs=$TESTPOOL/${filesystems[$(($fscount - $i - 1))]}
		mnt=$path/${filesystems[$i]}
		zfs set mountpoint=$mnt $fs
	done

	zfs list -r $TESTPOOL

	return 0
}

function cleanup_all
{
	export __ZFS_POOL_RESTRICT="$TESTPOOL"
	log_must zfs $unmountall
	unset __ZFS_POOL_RESTRICT

	for fs in ${filesystems[@]}; do
		cleanup_filesystem "$TESTPOOL" "$fs"
	done
	[[ -d ${TEST_BASE_DIR%%/}/testroot$$ ]] && \
		rm -rf ${TEST_BASE_DIR%%/}/testroot$$
}

#
# This function takes a single true/false argument. If true it will verify that
# all file systems are mounted. If false it will verify that they are not
# mounted.
#
function verify_all
{
	if $1; then
		logfunc=log_must
	else
		logfunc=log_mustnot
	fi

	for fs in ${filesystems[@]}; do
		$logfunc mounted "$TESTPOOL/$fs"
	done

	return 0
}

log_onexit cleanup_all

log_must setup_all

export __ZFS_POOL_RESTRICT="$TESTPOOL"
log_must zfs $unmountall
unset __ZFS_POOL_RESTRICT

verify_all false

export __ZFS_POOL_RESTRICT="$TESTPOOL"
log_must zfs $mountall
unset __ZFS_POOL_RESTRICT

verify_all true

log_note "Verify that 'zfs $mountcmd' will display " \
	"all ZFS filesystems currently mounted."

verify_mount_display

log_pass "'zfs $mountall' succeeds as root, " \
	"and all available ZFS filesystems are mounted."
