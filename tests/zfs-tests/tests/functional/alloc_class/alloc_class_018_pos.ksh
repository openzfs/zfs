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

. $STF_SUITE/tests/functional/alloc_class/alloc_class.kshlib

#
# DESCRIPTION:
#	Special and dedup devices are each compared with the normal vdevs,
#	not with each other.  A pool with special and dedup mirrors of
#	different widths, each at least as redundant as the normal vdevs,
#	can be created in one step or built up with 'zpool add', and is
#	considered consistent, so a later non-redundant addition is still
#	rejected without -f.
#
# STRATEGY:
#	1. Create a 2-way mirror pool with a 3-way special and a 2-way dedup
#	   mirror in one step.
#	2. Verify adding a single disk to that pool fails without -f.
#	3. Build the same pool with 'zpool add' and verify the same.
#	4. Verify a less redundant dedup device is rejected when it is added
#	   together with a more redundant special device.
#

verify_runnable "global"

EXTRA_DISK="$TEST_BASE_DIR/device-7"

function cleanup_018
{
	cleanup
	rm -f $EXTRA_DISK
}

claim="Special and dedup devices are compared with the normal vdevs."

log_assert $claim
log_onexit cleanup_018

log_must disk_setup
log_must truncate -s $CLASS_DEVSIZE $EXTRA_DISK

# A 2-way mirror tolerates one failure, as does the 2-way dedup mirror,
# and the 3-way special mirror tolerates two.
log_must zpool create $TESTPOOL mirror $ZPOOL_DISK0 $ZPOOL_DISK1 \
    special mirror $CLASS_DISK0 $CLASS_DISK1 $CLASS_DISK2 \
    dedup mirror $CLASS_DISK3 $EXTRA_DISK
log_mustnot zpool add $TESTPOOL $ZPOOL_DISK2
log_must zpool destroy -f $TESTPOOL

log_must zpool create $TESTPOOL mirror $ZPOOL_DISK0 $ZPOOL_DISK1 \
    special mirror $CLASS_DISK0 $CLASS_DISK1 $CLASS_DISK2
log_must zpool add $TESTPOOL dedup mirror $CLASS_DISK3 $EXTRA_DISK
log_mustnot zpool add $TESTPOOL $ZPOOL_DISK2
log_must zpool add -f $TESTPOOL $ZPOOL_DISK2
log_must zpool destroy -f $TESTPOOL

log_must zpool create $TESTPOOL mirror $ZPOOL_DISK0 $ZPOOL_DISK1
log_must zpool add $TESTPOOL \
    dedup mirror $CLASS_DISK3 $EXTRA_DISK \
    special mirror $CLASS_DISK0 $CLASS_DISK1 $CLASS_DISK2
log_mustnot zpool add $TESTPOOL $ZPOOL_DISK2
log_must zpool destroy -f $TESTPOOL

# A non-redundant dedup device is rejected even when it is followed by a
# more redundant special device.
log_must zpool create $TESTPOOL mirror $ZPOOL_DISK0 $ZPOOL_DISK1
log_mustnot zpool add $TESTPOOL dedup $CLASS_DISK3 \
    special mirror $CLASS_DISK0 $CLASS_DISK1 $CLASS_DISK2
log_must zpool destroy -f $TESTPOOL

log_pass $claim
