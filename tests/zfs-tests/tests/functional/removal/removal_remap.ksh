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

# N.B. The 'zfs remap' command has been disabled and may be removed.
export ZFS_REMAP_ENABLED=YES

default_setup_noexit "$DISKS"


function cleanup
{
	set_tunable64 zfs_condense_min_mapping_bytes 131072
	default_cleanup_noexit
}

log_onexit cleanup

log_must set_tunable64 zfs_condense_min_mapping_bytes 1

log_must zfs set recordsize=512 $TESTPOOL/$TESTFS

#
# Create a large file so that we know some of the blocks will be on the
# removed device, and hence eligible for remapping.
#
log_must dd if=/dev/urandom of=$TESTDIR/file bs=$((2**12)) count=$((2**9))

#
# Randomly rewrite some of blocks in the file so that there will be holes and
# we will not be able to remap the entire file in a few huge chunks.
#
for i in $(seq $((2**12))); do
	#
	# We have to sync periodically so that all the writes don't end up in
	# the same txg. If they were all in the same txg, only the last write
	# would go through and we would not have as many allocations to
	# fragment the file.
	#
	((i % 100 > 0 )) || sync_pool || log_fail "Could not sync."
        random_write $TESTDIR/file $((2**9)) || \
            log_fail "Could not random write."
done

#
# Remap should quietly succeed as a noop before a removal.
#
log_must zfs remap $TESTPOOL/$TESTFS
remaptxg_before=$(zfs get -H -o value remaptxg $TESTPOOL/$TESTFS)
(( $? == 0 )) || log_fail "Could not get remaptxg."
[[ $remaptxg_before == "-" ]] || \
    log_fail "remaptxg ($remaptxg_before) had value before a removal"

log_must zpool remove $TESTPOOL $REMOVEDISK
log_must wait_for_removal $TESTPOOL
log_mustnot vdevs_in_pool $TESTPOOL $REMOVEDISK

#
# remaptxg should not be set if we haven't done a remap.
#
remaptxg_before=$(zfs get -H -o value remaptxg $TESTPOOL/$TESTFS)
(( $? == 0 )) || log_fail "Could not get remaptxg."
[[ $remaptxg_before == "-" ]] || \
    log_fail "remaptxg ($remaptxg_before) had value before a removal"

mapping_size_before=$(indirect_vdev_mapping_size $TESTPOOL)
log_must zfs remap $TESTPOOL/$TESTFS

# Try to wait for a condense to finish.
for i in {1..5}; do
	sleep 5
	sync_pool
done
mapping_size_after=$(indirect_vdev_mapping_size $TESTPOOL)

#
# After the remap, there should not be very many blocks referenced. The reason
# why our threshold is as high as 512 is because our ratio of metadata to
# user data is relatively high, with only 64M of user data on the file system.
#
(( mapping_size_after < mapping_size_before )) || \
    log_fail "Mapping size did not decrease after remap: " \
    "$mapping_size_before before to $mapping_size_after after."
(( mapping_size_after < 512 )) || \
    log_fail "Mapping size not small enough after remap: " \
    "$mapping_size_before before to $mapping_size_after after."

#
# After a remap, the remaptxg should be set to a non-zero value.
#
remaptxg_after=$(zfs get -H -o value remaptxg $TESTPOOL/$TESTFS)
(( $? == 0 )) || log_fail "Could not get remaptxg."
log_note "remap txg after remap is $remaptxg_after"
(( remaptxg_after > 0 )) || log_fail "remaptxg not increased"

#
# Remap should quietly succeed as a noop if there have been no removals since
# the last remap.
#
log_must zfs remap $TESTPOOL/$TESTFS
remaptxg_again=$(zfs get -H -o value remaptxg $TESTPOOL/$TESTFS)
(( $? == 0 )) || log_fail "Could not get remaptxg."
log_note "remap txg after second remap is $remaptxg_again"
(( remaptxg_again == remaptxg_after )) || \
    log_fail "remap not noop if there has been no removal"

log_pass "Remapping a fs caused mapping size to decrease."
