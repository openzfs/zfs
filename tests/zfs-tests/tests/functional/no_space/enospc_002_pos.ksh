#!/bin/ksh -p
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
. $STF_SUITE/tests/functional/no_space/enospc.cfg

#
# DESCRIPTION:
# After filling a filesystem, certain zfs commands are allowed.
#

verify_runnable "both"

function cleanup
{
	log_must_busy zpool destroy -f $TESTPOOL
}

log_onexit cleanup

log_assert "ENOSPC is returned when file system is full."

default_setup_noexit $DISK_SMALL
log_must zfs set compression=off $TESTPOOL/$TESTFS
log_must zfs snapshot $TESTPOOL/$TESTFS@snap

#
# Completely fill the pool in order to ensure the commands below will more
# reliably succeed or fail as a result of lack of space.  Care is taken to
# force multiple transaction groups to ensure as many recently freed blocks
# as possible are reallocated.
#
log_note "Writing files until ENOSPC."

for i in $(seq 100); do
	file_write -o create -f $TESTDIR/file.$i -b $BLOCKSZ \
	    -c $NUM_WRITES -d $DATA
	ret=$?
	(( $ret != $ENOSPC )) && \
	    log_fail "file.$i returned: $ret rather than ENOSPC."

	sync_all_pools true
done

log_mustnot_expect space zfs create $TESTPOOL/$TESTFS/subfs
log_mustnot_expect space zfs clone $TESTPOOL/$TESTFS@snap $TESTPOOL/clone
log_mustnot_expect space zfs snapshot $TESTPOOL/$TESTFS@snap2
log_mustnot_expect space zfs bookmark \
    $TESTPOOL/$TESTFS@snap $TESTPOOL/$TESTFS#bookmark

log_must zfs send $TESTPOOL/$TESTFS@snap > $TEST_BASE_DIR/stream.$$
log_mustnot_expect space zfs receive $TESTPOOL/$TESTFS/recvd < $TEST_BASE_DIR/stream.$$
log_must rm $TEST_BASE_DIR/stream.$$

log_must zfs rename $TESTPOOL/$TESTFS@snap $TESTPOOL/$TESTFS@snap_newname
log_must zfs rename $TESTPOOL/$TESTFS@snap_newname $TESTPOOL/$TESTFS@snap
log_must zfs rename $TESTPOOL/$TESTFS $TESTPOOL/${TESTFS}_newname
log_must zfs rename $TESTPOOL/${TESTFS}_newname $TESTPOOL/$TESTFS
log_must zfs allow staff snapshot $TESTPOOL/$TESTFS
log_must zfs unallow staff snapshot $TESTPOOL/$TESTFS
log_must zfs set user:prop=value $TESTPOOL/$TESTFS
log_must zfs set quota=1EB $TESTPOOL/$TESTFS
log_must zfs set quota=none $TESTPOOL/$TESTFS
log_must zfs set reservation=1KB $TESTPOOL/$TESTFS
log_must zfs set reservation=none $TESTPOOL/$TESTFS
log_must zpool scrub $TESTPOOL
zpool scrub -s $TESTPOOL
log_must zpool set comment="Use the force, Luke." $TESTPOOL
log_must zpool set comment="" $TESTPOOL

# destructive tests must come last
log_must zfs rollback $TESTPOOL/$TESTFS@snap
log_must zfs destroy $TESTPOOL/$TESTFS@snap

log_pass "ENOSPC returned as expected."
