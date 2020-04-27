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
#       Verify that if 'zfs mount -a' fails to mount one filesystem,
#       the command fails with a non-zero error code, but all other
#       filesystems are mounted.
#
# STRATEGY:
#       1. Create zfs filesystems
#       2. Unmount a leaf filesystem
#       3. Create a file in the above filesystem's mountpoint
#       4. Verify that 'zfs mount -a' succeeds if overlay=on and
#          fails to mount the above if overlay=off
#       5. Verify that all other filesystems were mounted
#

verify_runnable "both"

typeset -a filesystems
typeset path=${TEST_BASE_DIR%%/}/testroot$$/$TESTPOOL
typeset fscount=10

function setup_all
{
	# Create $fscount filesystems at the top level of $path
	for ((i=0; i<$fscount; i++)); do
		setup_filesystem "$DISKS" "$TESTPOOL" $i "$path/$i" ctr
	done

	zfs list -r $TESTPOOL

	return 0
}

function cleanup_all
{
	export __ZFS_POOL_RESTRICT="$TESTPOOL"
	log_must zfs $unmountall
	unset __ZFS_POOL_RESTRICT

	[[ -d ${TEST_BASE_DIR%%/}/testroot$$ ]] && \
		rm -rf ${TEST_BASE_DIR%%/}/testroot$$
}

log_onexit cleanup_all

log_must setup_all

#
# Unmount all of the above so that we can create the stray file
# in one of the mountpoint directories.
#
export __ZFS_POOL_RESTRICT="$TESTPOOL"
log_must zfs $unmountall
unset __ZFS_POOL_RESTRICT

# All of our filesystems should be unmounted at this point
for ((i=0; i<$fscount; i++)); do
	log_mustnot mounted "$TESTPOOL/$i"
done

# Create a stray file in one filesystem's mountpoint
touch $path/0/strayfile

export __ZFS_POOL_RESTRICT="$TESTPOOL"

# Verify that zfs mount -a succeeds with overlay=on (default)
log_must zfs $mountall
log_must mounted "$TESTPOOL/0"
log_must zfs $unmountall

# Verify that zfs mount -a succeeds with overlay=off
log_must zfs set overlay=off "$TESTPOOL/0"
log_mustnot zfs $mountall
log_mustnot mounted "$TESTPOOL/0"

unset __ZFS_POOL_RESTRICT

# All other filesystems should be mounted
for ((i=1; i<$fscount; i++)); do
	log_must mounted "$TESTPOOL/$i"
done

log_pass "'zfs $mountall' behaves as expected."
