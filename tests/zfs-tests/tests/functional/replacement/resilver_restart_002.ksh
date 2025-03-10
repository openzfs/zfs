#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0

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
# Copyright (c) 2020, Datto Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/replacement/replacement.cfg

#
# DESCRIPTION:
# Testing resilver completes when scan errors are encountered, but relevant
# DTL's have not been lost.
#
# STRATEGY:
# 1. Create a pool (1k recordsize)
# 2. Create a 32m file (32k records)
# 3. Inject an error halfway through the file
# 4. Start a resilver, ensure the error is triggered and that the resilver
#    does not restart after finishing
#
# NB: use legacy scanning to ensure scan of specific block causes error
#

function cleanup
{
	log_must zinject -c all
	destroy_pool $TESTPOOL1
	rm -f ${VDEV_FILES[@]} $SPARE_VDEV_FILE
	log_must set_tunable32 SCAN_LEGACY $ORIG_SCAN_LEGACY
}

log_assert "Check for resilver restarts caused by scan errors"

ORIG_SCAN_LEGACY=$(get_tunable SCAN_LEGACY)

log_onexit cleanup

# use legacy scan to ensure injected error will be triggered
log_must set_tunable32 SCAN_LEGACY 1

 # create the pool and a 32M file (32k blocks)
log_must truncate -s $VDEV_FILE_SIZE ${VDEV_FILES[0]} $SPARE_VDEV_FILE
log_must zpool create -f -O recordsize=1k $TESTPOOL1 ${VDEV_FILES[0]}
log_must eval "dd if=/dev/urandom of=/$TESTPOOL1/file bs=1M count=32 2>/dev/null"

# determine objset/object
objset=$(zdb -d $TESTPOOL1/ | sed -ne 's/.*ID \([0-9]*\).*/\1/p')
object=$(ls -i /$TESTPOOL1/file | awk '{print $1}')

# inject event to cause error during resilver
log_must zinject -b $(printf "%x:%x:0:3fff" $objset $object) $TESTPOOL1

# clear events and start resilver
log_must zpool events -c
log_must zpool attach $TESTPOOL1 ${VDEV_FILES[0]} $SPARE_VDEV_FILE

log_note "waiting for read errors to start showing up"
for iter in {0..59}
do
	sync_pool $TESTPOOL1
	err=$(zpool status $TESTPOOL1 | awk -v dev=${VDEV_FILES[0]} '$0 ~ dev {print $3}')
	(( $err > 0 )) && break
	sleep 1
done

(( $err == 0 )) && log_fail "Unable to induce errors in resilver"

log_note "waiting for resilver to finish"
for iter in {0..59}
do
	finish=$(zpool events | grep -cF "sysevent.fs.zfs.resilver_finish")
	(( $finish > 0 )) && break
	sleep 1
done

(( $finish == 0 )) && log_fail "resilver took too long to finish"

# wait a few syncs to ensure that zfs does not restart the resilver
sync_pool $TESTPOOL1
sync_pool $TESTPOOL1

# check if resilver was restarted
start=$(zpool events | grep -cF "sysevent.fs.zfs.resilver_start")
(( $start != 1 )) && log_fail "resilver restarted unnecessarily"

log_pass "Resilver did not restart unnecessarily from scan errors"
