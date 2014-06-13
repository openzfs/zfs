#! /usr/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that ZFS volume snapshot could be fscked
#
# STRATEGY:
# 1. Create a ZFS volume
# 2. Copy some files and create snapshot
# 3. Verify fsck on the snapshot is OK
#

verify_runnable "global"

function cleanup
{
	snapexists $TESTPOOL/$TESTVOL@snap && \
		$ZFS destroy $TESTPOOL/$TESTVOL@snap

	ismounted $TESTDIR ufs
	(( $? == 0 )) && log_must $UMOUNT $TESTDIR

	[[ -e $TESTDIR ]] && $RM -rf $TESTDIR
}

log_assert "Verify that ZFS volume snapshot could be fscked"
log_onexit cleanup

TESTVOL='testvol'
BLOCKSZ=$(( 1024 * 1024 ))
NUM_WRITES=40

$ECHO "y" | $NEWFS -v /dev/zvol/rdsk/$TESTPOOL/$TESTVOL >/dev/null 2>&1
(( $? != 0 )) && log_fail "Unable to newfs(1M) $TESTPOOL/$TESTVOL"

log_must $MKDIR $TESTDIR
log_must $MOUNT /dev/zvol/dsk/$TESTPOOL/$TESTVOL $TESTDIR

typeset -i fn=0
typeset -i retval=0

while (( 1 )); do
        $FILE_WRITE -o create -f $TESTDIR/testfile$$.$fn \
            -b $BLOCKSZ -c $NUM_WRITES
        retval=$?
        if (( $retval != 0 )); then
                break
        fi

        (( fn = fn + 1 ))
done

log_must $LOCKFS -f $TESTDIR
log_must $ZFS snapshot $TESTPOOL/$TESTVOL@snap

$FSCK -n /dev/zvol/rdsk/$TESTPOOL/$TESTVOL@snap >/dev/null 2>&1
retval=$?
(( $retval == 39 )) || log_fail "$FSCK exited with wrong value $retval "

log_pass "Verify that ZFS volume snapshot could be fscked"
