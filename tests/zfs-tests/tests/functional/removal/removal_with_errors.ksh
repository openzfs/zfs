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
# This test ensures the device removal is cancelled when hard IO
# errors are encountered during the removal process.  This is done
# to ensure that when removing a device all of the data is copied.
#
# STRATEGY:
#
# 1. We create a pool with enough redundancy such that IO errors
#    will not result in the pool being suspended.
# 2. We write some test data to the pool.
# 3. We inject READ errors in to one half of the top-level mirror-0
#    vdev which is being removed.  Then we start the removal process.
# 4. Verify that the injected read errors cause the removal of
#    mirror-0 to be cancelled and that mirror-0 has not been removed.
# 5. Clear the read fault injection.
# 6. Repeat steps 3-6 above except inject WRITE errors on one of
#    child vdevs in the destination mirror-1.
# 7. Lastly verify the pool data is still intact.
#

DISKDIR=$(mktemp -d)
DISK0=$DISKDIR/dsk0
DISK1=$DISKDIR/dsk1
DISK2=$DISKDIR/dsk2
DISK3=$DISKDIR/dsk3

log_must truncate -s $MINVDEVSIZE $DISK0 $DISK1
log_must truncate -s $((MINVDEVSIZE * 4)) $DISK2 $DISK3

function cleanup
{
	log_must zinject -c all
	default_cleanup_noexit
	log_must rm -rf $DISKDIR
}

function wait_for_removing_cancel
{
	typeset pool=$1

	log_must zpool wait -t remove $pool

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
log_must zfs set compression=off $TESTPOOL

FILE_CONTENTS="Leeloo Dallas mul-ti-pass."

echo $FILE_CONTENTS  >$TESTDIR/$TESTFILE0
log_must [ "x$(<$TESTDIR/$TESTFILE0)" = "x$FILE_CONTENTS" ]
log_must file_write -o create -f $TESTDIR/$TESTFILE1 -b $((2**20)) -c $((2**8))

# Flush the ARC to minimize cache effects.
log_must zpool export $TESTPOOL
log_must zpool import -d $DISKDIR $TESTPOOL

# Verify that unexpected read errors automatically cancel the removal.
log_must zinject -d $DISK0 -e io -T all -f 100 $TESTPOOL
log_must zpool remove $TESTPOOL mirror-0
log_must wait_for_removing_cancel $TESTPOOL
log_must vdevs_in_pool $TESTPOOL mirror-0
log_must zinject -c all

# Flush the ARC to minimize cache effects.
log_must zpool export $TESTPOOL
log_must zpool import -d $DISKDIR $TESTPOOL

# Verify that unexpected write errors automatically cancel the removal.
log_must zinject -d $DISK3 -e io -T all -f 100 $TESTPOOL
log_must zpool remove $TESTPOOL mirror-0
log_must wait_for_removing_cancel $TESTPOOL
log_must vdevs_in_pool $TESTPOOL mirror-0
log_must zinject -c all

log_must dd if=$TESTDIR/$TESTFILE0 of=/dev/null
log_must [ "x$(<$TESTDIR/$TESTFILE0)" = "x$FILE_CONTENTS" ]
log_must dd if=$TESTDIR/$TESTFILE1 of=/dev/null

log_pass "Device not removed due to unexpected errors."
