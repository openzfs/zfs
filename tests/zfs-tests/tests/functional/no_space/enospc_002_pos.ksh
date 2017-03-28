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

log_assert "ENOSPC is returned when file system is full."
sync
log_must zfs set compression=off $TESTPOOL/$TESTFS
log_must zfs snapshot $TESTPOOL/$TESTFS@snap

log_note "Writing file: $TESTFILE0 until ENOSPC."
file_write -o create -f $TESTDIR/$TESTFILE0 -b $BLOCKSZ \
    -c $NUM_WRITES -d $DATA
ret=$?

(( $ret != $ENOSPC )) && \
    log_fail "$TESTFILE0 returned: $ret rather than ENOSPC."

log_mustnot_expect space zfs create $TESTPOOL/$TESTFS/subfs
log_mustnot_expect space zfs clone $TESTPOOL/$TESTFS@snap $TESTPOOL/clone
log_mustnot_expect space zfs snapshot $TESTPOOL/$TESTFS@snap2
log_mustnot_expect space zfs bookmark \
    $TESTPOOL/$TESTFS@snap $TESTPOOL/$TESTFS#bookmark

log_must zfs send $TESTPOOL/$TESTFS@snap >/tmp/stream.$$
log_mustnot_expect space zfs receive $TESTPOOL/$TESTFS/recvd </tmp/stream.$$
log_must rm /tmp/stream.$$

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
