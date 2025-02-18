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
# Copyright (c) 2014, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
#
# For device removal a file's contents should transfer
# completely from one disk to another. That should remain
# to be the case even if multiple levels of indirection
# are introduced as we remove more and more devices.
#
# STRATEGY:
#
# 1. We create a file of size 128k and we save its contents
#    in a local variable.
# 2. We set the limit of the maximum copied segment size of
#    removals to 32k, so during removal our 128k file will
#    be split to 4 blocks.
# 3. We start removing disks and adding them back in a loop.
#    This way the file is moved around and introduces split
#    blocks.
# 4. The loop itself tests that we don't have any problem
#    when removing many devices. Within the loop we test
#    that the files contents remain the same across transfers.
#

TMPDIR=${TMPDIR:-$TEST_BASE_DIR}
log_must mkfile $(($MINVDEVSIZE * 2)) $TMPDIR/dsk1
log_must mkfile $(($MINVDEVSIZE * 2)) $TMPDIR/dsk2
DISKS="$TMPDIR/dsk1 $TMPDIR/dsk2"
REMOVEDISK=$TMPDIR/dsk1

log_must default_setup_noexit "$DISKS"

function cleanup
{
	default_cleanup_noexit
	log_must rm -f $DISKS

	# reset REMOVE_MAX_SEGMENT to 1M
	set_tunable32 REMOVE_MAX_SEGMENT 1048576
}

log_onexit cleanup

# set REMOVE_MAX_SEGMENT to 32k
log_must set_tunable32 REMOVE_MAX_SEGMENT 32768

log_must dd if=/dev/urandom of=$TESTDIR/$TESTFILE0 bs=128k count=1
FILE_CONTENTS=$(<$TESTDIR/$TESTFILE0)
log_must [ "x$(<$TESTDIR/$TESTFILE0)" = "x$FILE_CONTENTS" ]

for i in {1..10}; do
	log_must zpool remove $TESTPOOL $TMPDIR/dsk1
	log_must wait_for_removal $TESTPOOL
	log_mustnot vdevs_in_pool $TESTPOOL $TMPDIR/dsk1
	log_must zpool add $TESTPOOL $TMPDIR/dsk1

	log_must zinject -a
	log_must dd if=$TESTDIR/$TESTFILE0 of=/dev/null
	log_must [ "x$(<$TESTDIR/$TESTFILE0)" = "x$FILE_CONTENTS" ]

	log_must zpool remove $TESTPOOL $TMPDIR/dsk2
	log_must wait_for_removal $TESTPOOL
	log_mustnot vdevs_in_pool $TESTPOOL $TMPDIR/dsk2
	log_must zpool add $TESTPOOL $TMPDIR/dsk2

	log_must zinject -a
	log_must dd if=$TESTDIR/$TESTFILE0 of=/dev/null
	log_must [ "x$(<$TESTDIR/$TESTFILE0)" = "x$FILE_CONTENTS" ]
done

log_pass "File contents transferred completely from one disk to another."
