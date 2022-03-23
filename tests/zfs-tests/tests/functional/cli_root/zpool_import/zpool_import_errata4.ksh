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
# Copyright (c) 2019 Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zpool import' should import a pool with Errata #4. Users should be
# able to set the zfs_disable_ivset_guid_check to continue normal
# operation and the errata should disappear when no more effected
# datasets remain.
#
# STRATEGY:
# 1. Import a pre-packaged pool with Errata #4 and verify its state
# 2. Prepare pool to fix existing datasets
# 3. Use raw sends to fix datasets
# 4. Ensure fixed datasets match their initial counterparts
# 5. Destroy the initial datasets and verify the errata is gone
#

verify_runnable "global"

POOL_NAME=missing_ivset
POOL_FILE=missing_ivset.dat

function uncompress_pool
{
	log_note "Creating pool from $POOL_FILE"
	log_must eval bzcat \
	    $STF_SUITE/tests/functional/cli_root/zpool_import/blockfiles/$POOL_FILE.bz2 \
	    "> /$TESTPOOL/$POOL_FILE"
}

function cleanup
{
	log_must set_tunable32 DISABLE_IVSET_GUID_CHECK 0
	poolexists $POOL_NAME && log_must zpool destroy $POOL_NAME
	log_must rm -rf /$TESTPOOL/$POOL_FILE
}
log_onexit cleanup

log_assert "Verify that Errata 4 is properly handled"

function has_ivset_guid # dataset
{
	ds="$1"
	ivset_guid=$(get_prop ivsetguid $ds)

	[ "$ivset_guid" != "-" ]
}

# 1. Import a pre-packaged pool with Errata #4 and verify its state
uncompress_pool
log_must zpool import -d /$TESTPOOL/ $POOL_NAME
log_must eval "zpool status $POOL_NAME | grep -q 'Errata #4'"
log_must eval "zpool status $POOL_NAME | grep -q ZFS-8000-ER"
bm2_value=$(zpool get -H -o value feature@bookmark_v2 $POOL_NAME)
log_must [ "$bm2_value" = "disabled" ]

log_mustnot has_ivset_guid $POOL_NAME/testfs@snap1
log_mustnot has_ivset_guid $POOL_NAME/testfs@snap2
log_mustnot has_ivset_guid $POOL_NAME/testfs@snap3
log_mustnot has_ivset_guid $POOL_NAME/testvol@snap1
log_mustnot has_ivset_guid $POOL_NAME/testvol@snap2
log_mustnot has_ivset_guid $POOL_NAME/testvol@snap3

# 2. Prepare pool to fix existing datasets
log_must zpool set feature@bookmark_v2=enabled $POOL_NAME
log_must set_tunable32 DISABLE_IVSET_GUID_CHECK 1
log_must zfs create $POOL_NAME/fixed

# 3. Use raw sends to fix datasets
log_must eval "zfs send -w $POOL_NAME/testfs@snap1 | \
	zfs recv $POOL_NAME/fixed/testfs"
log_must eval "zfs send -w -i @snap1 $POOL_NAME/testfs@snap2 | \
	zfs recv $POOL_NAME/fixed/testfs"
log_must eval \
	"zfs send -w -i $POOL_NAME/testfs#snap2 $POOL_NAME/testfs@snap3 | \
	zfs recv $POOL_NAME/fixed/testfs"

log_must eval "zfs send -w $POOL_NAME/testvol@snap1 | \
	zfs recv $POOL_NAME/fixed/testvol"
log_must eval "zfs send -w -i @snap1 $POOL_NAME/testvol@snap2 | \
	zfs recv $POOL_NAME/fixed/testvol"
log_must eval \
	"zfs send -w -i $POOL_NAME/testvol#snap2 $POOL_NAME/testvol@snap3 | \
	zfs recv $POOL_NAME/fixed/testvol"

# 4. Ensure fixed datasets match their initial counterparts
log_must eval "echo 'password' | zfs load-key $POOL_NAME/testfs"
log_must eval "echo 'password' | zfs load-key $POOL_NAME/testvol"
log_must eval "echo 'password' | zfs load-key $POOL_NAME/fixed/testfs"
log_must eval "echo 'password' | zfs load-key $POOL_NAME/fixed/testvol"
log_must zfs mount $POOL_NAME/testfs
log_must zfs mount $POOL_NAME/fixed/testfs
block_device_wait

old_mntpnt=$(get_prop mountpoint $POOL_NAME/testfs)
new_mntpnt=$(get_prop mountpoint $POOL_NAME/fixed/testfs)
log_must directory_diff "$old_mntpnt" "$new_mntpnt"
log_must diff /dev/zvol/$POOL_NAME/testvol /dev/zvol/$POOL_NAME/fixed/testvol

log_must has_ivset_guid $POOL_NAME/fixed/testfs@snap1
log_must has_ivset_guid $POOL_NAME/fixed/testfs@snap2
log_must has_ivset_guid $POOL_NAME/fixed/testfs@snap3
log_must has_ivset_guid $POOL_NAME/fixed/testvol@snap1
log_must has_ivset_guid $POOL_NAME/fixed/testvol@snap2
log_must has_ivset_guid $POOL_NAME/fixed/testvol@snap3

# 5. Destroy the initial datasets and verify the errata is gone
log_must zfs destroy -r $POOL_NAME/testfs
log_must zfs destroy -r $POOL_NAME/testvol

log_must zpool export $POOL_NAME
log_must zpool import -d /$TESTPOOL/ $POOL_NAME
log_mustnot eval "zpool status $POOL_NAME | grep -q 'Errata #4'"
log_mustnot eval "zpool status $POOL_NAME | grep -q ZFS-8000-ER"
log_pass "Errata 4 is properly handled"
