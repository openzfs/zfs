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
# Copyright (c) 2014, 2017 by Delphix. All rights reserved.
# Copyright (c) 2018 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

TMPDIR=${TMPDIR:-$TEST_BASE_DIR}
DISK0=$TMPDIR/dsk0
DISK1=$TMPDIR/dsk1
DISK2=$TMPDIR/dsk2
DISK3=$TMPDIR/dsk3

log_must truncate -s $MINVDEVSIZE $DISK0 $DISK1
log_must truncate -s $((MINVDEVSIZE * 4)) $DISK2 $DISK3

function cleanup
{
	log_must zinject -c all
	default_cleanup_noexit
	log_must rm -f $DISK0 $DISK1 $DISK2 $DISK3
}

function wait_for_removing_cancel
{
	typeset pool=$1

	while is_pool_removing $pool; do
		sleep 1
	done

	#
	# The pool state changes before the TXG finishes syncing; wait for
	# the removal to be completed on disk.
	#
	sync_pool

	log_mustnot is_pool_removed $pool
	return 0
}

default_setup_noexit "mirror $DISK0 $DISK1 mirror $DISK2 $DISK3"
log_onexit cleanup

FILE_CONTENTS="Leeloo Dallas mul-ti-pass."

echo $FILE_CONTENTS  >$TESTDIR/$TESTFILE0
log_must [ "x$(<$TESTDIR/$TESTFILE0)" = "x$FILE_CONTENTS" ]
log_must file_write -o create -f $TESTDIR/$TESTFILE1 -b $((2**20)) -c $((2**7))
sync_pool $TESTPOOL

# Verify that unexpected read errors automatically cancel the removal.
log_must zinject -d $DISK0 -e io -T all -f 100 $TESTPOOL
log_must zpool remove $TESTPOOL mirror-0
log_must wait_for_removing_cancel $TESTPOOL
log_must vdevs_in_pool $TESTPOOL mirror-0
log_must zinject -c all

# Verify that unexpected write errors automatically cancel the removal.
log_must zinject -d $DISK3 -e io -T all -f 100 $TESTPOOL
log_must zpool remove $TESTPOOL mirror-0
log_must wait_for_removing_cancel $TESTPOOL
log_must vdevs_in_pool $TESTPOOL mirror-0
log_must zinject -c all

log_must dd if=/$TESTDIR/$TESTFILE0 of=/dev/null
log_must [ "x$(<$TESTDIR/$TESTFILE0)" = "x$FILE_CONTENTS" ]

log_pass "Device not removed due to unexpected errors."
