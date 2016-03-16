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
# Copyright (c) 2014 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/no_space/enospc.cfg

#
# DESCRIPTION:
# After filling a filesystem, certain zfs commands are allowed.
#

verify_runnable "both"

log_assert "ENOSPC is returned when file system is full."
sync
log_must $ZFS set compression=off $TESTPOOL/$TESTFS
log_must $ZFS snapshot $TESTPOOL/$TESTFS@snap

log_note "Writing file: $TESTFILE0 until ENOSPC."
$FILE_WRITE -o create -f $TESTDIR/$TESTFILE0 -b $BLOCKSZ \
    -c $NUM_WRITES -d $DATA
ret=$?

(( $ret != $ENOSPC )) && \
    log_fail "$TESTFILE0 returned: $ret rather than ENOSPC."

log_mustnot_expect space $ZFS create $TESTPOOL/$TESTFS/subfs
log_mustnot_expect space $ZFS clone $TESTPOOL/$TESTFS@snap $TESTPOOL/clone
log_mustnot_expect space $ZFS snapshot $TESTPOOL/$TESTFS@snap2
log_mustnot_expect space $ZFS bookmark \
    $TESTPOOL/$TESTFS@snap $TESTPOOL/$TESTFS#bookmark

log_must $ZFS send $TESTPOOL/$TESTFS@snap >/tmp/stream.$$
log_mustnot_expect space $ZFS receive $TESTPOOL/$TESTFS/recvd </tmp/stream.$$
log_must rm /tmp/stream.$$

log_must $ZFS rename $TESTPOOL/$TESTFS@snap $TESTPOOL/$TESTFS@snap_newname
log_must $ZFS rename $TESTPOOL/$TESTFS@snap_newname $TESTPOOL/$TESTFS@snap
log_must $ZFS rename $TESTPOOL/$TESTFS $TESTPOOL/${TESTFS}_newname
log_must $ZFS rename $TESTPOOL/${TESTFS}_newname $TESTPOOL/$TESTFS
log_must $ZFS allow staff snapshot $TESTPOOL/$TESTFS
log_must $ZFS unallow staff snapshot $TESTPOOL/$TESTFS
log_must $ZFS set user:prop=value $TESTPOOL/$TESTFS
log_must $ZFS set quota=1EB $TESTPOOL/$TESTFS
log_must $ZFS set quota=none $TESTPOOL/$TESTFS
log_must $ZFS set reservation=1KB $TESTPOOL/$TESTFS
log_must $ZFS set reservation=none $TESTPOOL/$TESTFS
log_must $ZPOOL scrub $TESTPOOL
$ZPOOL scrub -s $TESTPOOL
log_must $ZPOOL set comment="Use the force, Luke." $TESTPOOL
log_must $ZPOOL set comment="" $TESTPOOL

# destructive tests must come last
log_must $ZFS rollback $TESTPOOL/$TESTFS@snap
log_must $ZFS destroy $TESTPOOL/$TESTFS@snap

log_pass "ENOSPC returned as expected."
