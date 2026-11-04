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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Verify that zpool export and zpool destroy of a pool imported
#	read-only succeed right after they unmount the pool's datasets.
#
# STRATEGY:
#	1. Create a pool with a file system holding many files
#	2. Import the pool read-only and stat every file, so the file
#	   system's objset has many dnodes to evict when it is unmounted
#	3. Export the pool; it must not fail with "pool is busy", and the
#	   pool must be gone afterwards
#	4. Repeat 2-3 a few times
#	5. Import it read-only once more, stat the files and destroy it,
#	   which takes the same path
#
# A dataset that is closed is evicted asynchronously, and its dsl_dir
# holds a reference on the pool until then.  Evicting a file system's
# cached dnodes takes long enough that the export's reference check runs
# before the eviction finishes, unless the export waits for it.
#

verify_runnable "global"

DEVICE_DIR=$TEST_BASE_DIR/dev_export-readonly-test
MNTPT=$TEST_BASE_DIR/export-readonly-mnt
NUM_FILES=10000

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	log_must rm -rf $DEVICE_DIR $MNTPT
}

log_assert "zpool export/destroy of a read-only pool succeeds right after unmount"

log_onexit cleanup

log_must mkdir -p $DEVICE_DIR
log_must truncate -s $MINVDEVSIZE $DEVICE_DIR/disk
log_must zpool create -f -O mountpoint=none $TESTPOOL1 $DEVICE_DIR/disk
log_must zfs create -o mountpoint=$MNTPT $TESTPOOL1/$TESTFS1
log_must eval "(cd $MNTPT && seq $NUM_FILES | xargs touch)"
log_must zpool export $TESTPOOL1

for i in {1..3}; do
	log_must zpool import -d $DEVICE_DIR -o readonly=on $TESTPOOL1
	log_must eval "ls -l $MNTPT >/dev/null"
	log_must zpool export $TESTPOOL1
	log_mustnot poolexists $TESTPOOL1
done

log_must zpool import -d $DEVICE_DIR -o readonly=on $TESTPOOL1
log_must eval "ls -l $MNTPT >/dev/null"
log_must zpool destroy $TESTPOOL1
log_mustnot poolexists $TESTPOOL1

log_pass "zpool export/destroy of a read-only pool succeeds right after unmount"
