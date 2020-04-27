#!/bin/ksh -p
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

#
# Copyright 2016 Nexenta Systems, Inc.
# Copyright (c) 2019 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/tests/functional/cli_root/zpool_labelclear/labelclear.cfg

# DESCRIPTION:
#    Check that `zpool labelclear` only clears valid labels.  Expected
#    label offsets which do not contain intact labels are left untouched.
#
# STRATEGY:
# 1. Create a pool with primary, log, spare and cache devices.
# 2. Export the pool.
# 3. Write a known pattern over the first two device labels.
# 4. Verify with zdb that only the last two device labels are intact.
# 5. Verify the pool could be imported using those labels.
# 6. Run `zpool labelclear` to destroy those last two labels.
# 7. Verify the pool can no longer be found; let alone imported.
# 8. Verify the pattern is intact to confirm `zpool labelclear` did
#    not write to first two label offsets.
# 9. Verify that no valid label remain.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -f $PATTERN_FILE $DISK_PATTERN_FILE \
	    $DEVICE1 $DEVICE2 $DEVICE3 $DEVICE4
}

log_onexit cleanup
log_assert "zpool labelclear will only clear valid labels"

PATTERN_FILE=$TEST_BASE_DIR/pattern
DISK_PATTERN_FILE=$TEST_BASE_DIR/disk-pattern

DEVICE1="$TEST_BASE_DIR/device-1"
DEVICE2="$TEST_BASE_DIR/device-2"
DEVICE3="$TEST_BASE_DIR/device-3"
DEVICE4="$TEST_BASE_DIR/device-4"

log_must dd if=/dev/urandom of=$PATTERN_FILE bs=1048576 count=4

log_must truncate -s $SPA_MINDEVSIZE $DEVICE1 $DEVICE2 $DEVICE3 $DEVICE4

log_must zpool create -O mountpoint=none -f $TESTPOOL $DEVICE1 \
     log $DEVICE2 cache $DEVICE3 spare $DEVICE4
log_must zpool export $TESTPOOL

# Overwrite the first 4M of each device and verify the expected labels.
for dev in $DEVICE1 $DEVICE2 $DEVICE3 $DEVICE4; do
	dd if=$PATTERN_FILE of=$dev bs=1048576 conv=notrunc
	log_must eval "zdb -l $dev | grep 'labels = 2 3'"
done

# Verify the pool could be imported using those labels.
log_must eval "zpool import -d $TEST_BASE_DIR | grep $TESTPOOL"

# Verify the last two labels on each vdev can be cleared.
for dev in $DEVICE1 $DEVICE2 $DEVICE3 $DEVICE4; do
	log_must zpool labelclear -f $dev
done

# Verify there is no longer a pool which can be imported.
log_mustnot eval "zpool import -d $TEST_BASE_DIR | grep $TESTPOOL"

# Verify the original pattern over the first two labels is intact
for dev in $DEVICE1 $DEVICE2 $DEVICE3 $DEVICE4; do
	log_must dd if=$dev of=$DISK_PATTERN_FILE bs=1048576 count=4
	log_must cmp $DISK_PATTERN_FILE $PATTERN_FILE
	log_mustnot zdb -lq $dev
done

# Verify an error is reported when there are no labels to clear.
for dev in $DEVICE1 $DEVICE2 $DEVICE3 $DEVICE4; do
	log_mustnot zpool labelclear -f $dev
done

log_pass "zpool labelclear will only clear valid labels"
