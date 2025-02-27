#! /bin/ksh -p
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
# Copyright (c) 2014, 2017 by Delphix. All rights reserved.
# Copyright (c) 2018 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
#
# This test ensures that even when child vdevs are unavailable the
# device removal process copies from readable source children to
# writable destination children.  This may be different than the
# default mapping which preferentially pairs up source and destination
# child vdevs based on their child ids.
#
# Default Mapping:
#   mirror-0                mirror-1
#     DISK0 (child 0) ------> DISK2 (child 0)
#     DISK1 (child 1) ------> DISK3 (child 1)
#
# We want to setup a scenario where the default mapping would make
# it impossible to copy any data during the removal process.  This
# is done by faulting both the mirror-0 (child 0) source vdev and
# mirror-1 (child 1) destination vdev.  As shown below the default
# mapping cannot be used due to the FAULTED vdevs.  Verify that an
# alternate mapping is selected and all the readable data is copied.
#
# Default Mapping (BAD):
#   mirror-0                mirror-1
#     DISK0 (FAULTED) ------> DISK2
#     DISK1 ----------------> DISK3 (FAULTED)
#
# Required Mapping (GOOD):
#   mirror-0                mirror-1
#     DISK0 (FAULTED)   +---> DISK2
#     DISK1 ------------+     DISK3 (FAULTED)
#
# STRATEGY:
#
# 1. We create a pool with two top-level mirror vdevs.
# 2. We write some test data to the pool.
# 3. We fault two children to force the scenario described above.
# 4. We remove the mirror-0 device.
# 5. We verify that the device has been removed and that all of the
#    data is still intact.
#

TMPDIR=${TMPDIR:-$TEST_BASE_DIR}
DISK0=$TMPDIR/dsk0
DISK1=$TMPDIR/dsk1
DISK2=$TMPDIR/dsk2
DISK3=$TMPDIR/dsk3

log_must truncate -s $MINVDEVSIZE $DISK0 $DISK1
log_must truncate -s $((MINVDEVSIZE * 4)) $DISK2 $DISK3

function cleanup
{
	default_cleanup_noexit
	log_must rm -f $DISK0 $DISK1 $DISK2 $DISK3
}

default_setup_noexit "mirror $DISK0 $DISK1 mirror $DISK2 $DISK3"
log_onexit cleanup

log_must zpool offline -f $TESTPOOL $DISK0
log_must zpool offline -f $TESTPOOL $DISK3

FILE_CONTENTS="Leeloo Dallas mul-ti-pass."

echo $FILE_CONTENTS  >$TESTDIR/$TESTFILE0
log_must [ "x$(<$TESTDIR/$TESTFILE0)" = "x$FILE_CONTENTS" ]
log_must file_write -o create -f $TESTDIR/$TESTFILE1 -b $((2**20)) -c $((2**7))
sync_pool $TESTPOOL

log_must zpool remove $TESTPOOL mirror-0
log_must wait_for_removal $TESTPOOL
log_mustnot vdevs_in_pool $TESTPOOL mirror-0

verify_pool $TESTPOOL

log_must dd if=$TESTDIR/$TESTFILE0 of=/dev/null
log_must [ "x$(<$TESTDIR/$TESTFILE0)" = "x$FILE_CONTENTS" ]
log_must dd if=$TESTDIR/$TESTFILE1 of=/dev/null

log_pass "Can remove with faulted vdevs"
