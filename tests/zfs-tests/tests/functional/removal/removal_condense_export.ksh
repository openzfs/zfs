#! /bin/ksh -p
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
# Copyright (c) 2015, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

if is_linux; then
	log_unsupported "ZDB fails during concurrent pool activity."
fi

function reset
{
	log_must set_tunable64 zfs_condense_indirect_commit_entry_delay_ms 0
	log_must set_tunable64 zfs_condense_min_mapping_bytes 131072
	default_cleanup_noexit
}

default_setup_noexit "$DISKS" "true"
log_onexit reset
log_must set_tunable64 zfs_condense_indirect_commit_entry_delay_ms 1000
log_must set_tunable64 zfs_condense_min_mapping_bytes 1

log_must zfs set recordsize=512 $TESTPOOL/$TESTFS

#
# Create a large file so that we know some of the blocks will be on the
# removed device, and hence eligible for remapping.
#
log_must dd if=/dev/urandom of=$TESTDIR/file bs=1024k count=10

#
# Create a file in the other filesystem, which will not be remapped.
#
log_must dd if=/dev/urandom of=$TESTDIR1/file bs=1024k count=10

#
# Randomly rewrite some of blocks in the file so that there will be holes and
# we will not be able to remap the entire file in a few huge chunks.
#
for i in {1..4096}; do
	#
	# We have to sync periodically so that all the writes don't end up in
	# the same txg. If they were all in the same txg, only the last write
	# would go through and we would not have as many allocations to
	# fragment the file.
	#
	((i % 100 > 0 )) || sync_pool $TESTPOOL || log_fail "Could not sync."
        random_write $TESTDIR/file 512 || \
            log_fail "Could not random write."
done

REMOVEDISKPATH=/dev
case $REMOVEDISK in
	/*)
		REMOVEDISKPATH=$(dirname $REMOVEDISK)
		;;
esac

log_must zpool remove $TESTPOOL $REMOVEDISK
log_must wait_for_removal $TESTPOOL
log_mustnot vdevs_in_pool $TESTPOOL $REMOVEDISK

log_must zfs remap $TESTPOOL/$TESTFS
sync_pool $TESTPOOL
sleep 5
sync_pool $TESTPOOL
log_must zpool export $TESTPOOL
zdb -e -p $REMOVEDISKPATH $TESTPOOL | grep 'Condensing indirect vdev' || \
    log_fail "Did not export during a condense."
log_must zdb -e -p $REMOVEDISKPATH -cudi $TESTPOOL
log_must zpool import $TESTPOOL

log_pass "Pool can be exported in the middle of a condense."
