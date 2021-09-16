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
# Copyright (c) 2017 Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zpool import' should import a pool with Errata #3 while preventing
# the user from performing read write operations
#
# STRATEGY:
# 1. Import a pre-packaged pool with Errata #3
# 2. Attempt to write to the effected datasets
# 3. Attempt to read from the effected datasets
# 4. Attempt to perform a raw send of the effected datasets
# 5. Perform a regular send of the datasets under a new encryption root
# 6. Verify the new datasets can be read from and written to
# 7. Destroy the old effected datasets
# 8. Reimport the pool and verify that the errata is no longer present
#

verify_runnable "global"

POOL_NAME=cryptv0
POOL_FILE=cryptv0.dat

function uncompress_pool
{
	log_note "Creating pool from $POOL_FILE"
	log_must bzcat \
	    $STF_SUITE/tests/functional/cli_root/zpool_import/blockfiles/$POOL_FILE.bz2 \
	    > /$TESTPOOL/$POOL_FILE
	return 0
}

function cleanup
{
	poolexists $POOL_NAME && log_must zpool destroy $POOL_NAME
	[[ -e /$TESTPOOL/$POOL_FILE ]] && rm /$TESTPOOL/$POOL_FILE
	return 0
}
log_onexit cleanup

log_assert "Verify that Errata 3 is properly handled"

uncompress_pool
log_must zpool import -d /$TESTPOOL/ $POOL_NAME
log_must eval "zpool status $POOL_NAME | grep -q Errata" # also detects 'Errata #4'
log_must eval "zpool status $POOL_NAME | grep -q ZFS-8000-ER"
log_must eval "echo 'password' | zfs load-key $POOL_NAME/testfs"
log_must eval "echo 'password' | zfs load-key $POOL_NAME/testvol"

log_mustnot zfs mount $POOL_NAME/testfs
log_must zfs mount -o ro $POOL_NAME/testfs

old_mntpnt=$(get_prop mountpoint $POOL_NAME/testfs)
log_must eval "ls $old_mntpnt | grep -q testfile"
block_device_wait /dev/zvol/$POOL_NAME/testvol
log_mustnot dd if=/dev/zero of=/dev/zvol/$POOL_NAME/testvol bs=512 count=1
log_must dd if=/dev/zvol/$POOL_NAME/testvol of=/dev/null bs=512 count=1

log_must zpool set feature@bookmark_v2=enabled $POOL_NAME # necessary for Errata #4

log_must eval "echo 'password' | zfs create \
	-o encryption=on -o keyformat=passphrase -o keylocation=prompt \
	$POOL_NAME/encroot"
log_mustnot eval "zfs send -w $POOL_NAME/testfs@snap1 | \
	zfs recv $POOL_NAME/encroot/testfs"
log_mustnot eval "zfs send -w $POOL_NAME/testvol@snap1 | \
	zfs recv $POOL_NAME/encroot/testvol"

log_must eval "zfs send $POOL_NAME/testfs@snap1 | \
	zfs recv $POOL_NAME/encroot/testfs"
log_must eval "zfs send $POOL_NAME/testvol@snap1 | \
	zfs recv $POOL_NAME/encroot/testvol"
block_device_wait /dev/zvol/$POOL_NAME/encroot/testvol
log_must dd if=/dev/zero of=/dev/zvol/$POOL_NAME/encroot/testvol bs=512 count=1
new_mntpnt=$(get_prop mountpoint $POOL_NAME/encroot/testfs)
log_must eval "ls $new_mntpnt | grep -q testfile"
log_must zfs destroy -r $POOL_NAME/testfs
log_must zfs destroy -r $POOL_NAME/testvol

log_must zpool export $POOL_NAME
log_must zpool import -d /$TESTPOOL/ $POOL_NAME
log_mustnot eval "zpool status $POOL_NAME | grep -q 'Errata #3'"
log_pass "Errata 3 is properly handled"
