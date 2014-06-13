#! /bin/ksh -p
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
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib
. $STF_SUITE/tests/functional/zvol/zvol_swap/zvol_swap.cfg

#
# DESCRIPTION:
# Verify that a zvol device can be used as a swap device
# through /etc/vfstab configuration.
#
# STRATEGY:
# 1. Modify /etc/vfstab to add the test zvol as swap device.
# 2. Use /sbin/swapadd to add zvol as swap device throuth /etc/vfstab
# 3. Create a file under /tmp and verify the file
#

verify_runnable "global"

function cleanup
{
	[[ -f /tmp/$TESTFILE ]] && log_must $RM -f /tmp/$TESTFILE
	[[ -f $NEW_VFSTAB_FILE ]] && log_must $RM -f $NEW_VFSTAB_FILE
	[[ -f $PREV_VFSTAB_FILE ]] && \
	    log_must $MV $PREV_VFSTAB_FILE $VFSTAB_FILE
	[[ -f $PREV_VFSTAB_FILE ]] && $RM -f $PREV_VFSTAB_FILE

	log_must $SWAPADD $VFSTAB_FILE

        if is_swap_inuse $voldev ; then
		log_must $SWAP -d $voldev
	fi

}

log_assert "Verify that a zvol device can be used as a swap device" \
    "through /etc/vfstab configuration."

log_onexit cleanup

voldev=/dev/zvol/dsk/$TESTPOOL/$TESTVOL
VFSTAB_FILE=/etc/vfstab
NEW_VFSTAB_FILE=/var/tmp/zvol_vfstab.$$
PREV_VFSTAB_FILE=/var/tmp/zvol_vfstab.PREV.$$

[[ -f $NEW_VFSTAB_FILE ]] && $CP /dev/null $NEW_VFSTAB_FILE

$AWK '{if ($4 != "swap") print $1}' /etc/vfstab > $NEW_VFSTAB_FILE
$ECHO "$voldev\t-\t-\tswap\t-\tno\t-"  >> $NEW_VFSTAB_FILE

# Copy off the original vfstab, and run swapadd on the newly constructed one.
log_must $CP $VFSTAB_FILE $PREV_VFSTAB_FILE
log_must $CP $NEW_VFSTAB_FILE $VFSTAB_FILE
log_must $SWAPADD $VFSTAB_FILE

log_must $FILE_WRITE -o create -f /tmp/$TESTFILE \
    -b $BLOCKSZ -c $NUM_WRITES -d $DATA

[[ ! -f /tmp/$TESTFILE ]] &&
    log_fail "Unable to create file under /tmp"

filesize=`$LS -l /tmp/$TESTFILE | $AWK '{print $5}'`
tf_size=$((BLOCKSZ * NUM_WRITES))
(($tf_size != $filesize)) && \
    log_fail "testfile is ($filesize bytes), expected ($tf_size bytes)"

log_pass "Successfully added a zvol to swap area through /etc/vfstab."
