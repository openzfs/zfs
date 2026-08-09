#! /bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
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
# 2. Use /sbin/swapadd to add zvol as swap device through /etc/vfstab
# 3. Create a file under /tmp and verify the file
#

verify_runnable "global"

function cleanup
{
	[[ -f $TESTDIR/$TESTFILE ]] && log_must rm -f $TESTDIR/$TESTFILE
	[[ -f $NEW_VFSTAB_FILE ]] && log_must rm -f $NEW_VFSTAB_FILE
	[[ -f $PREV_VFSTAB_FILE ]] && \
	    log_must mv $PREV_VFSTAB_FILE $VFSTAB_FILE
	[[ -f $PREV_VFSTAB_FILE ]] && rm -f $PREV_VFSTAB_FILE

	log_must swapadd $VFSTAB_FILE

	if is_swap_inuse $voldev ; then
		log_must swap -d $voldev
	fi

}

log_assert "Verify that a zvol device can be used as a swap device" \
    "through /etc/vfstab configuration."

log_onexit cleanup

voldev=${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL
VFSTAB_FILE=/etc/vfstab
NEW_VFSTAB_FILE=$TEST_BASE_DIR/zvol_vfstab.$$
PREV_VFSTAB_FILE=$TEST_BASE_DIR/zvol_vfstab.PREV.$$

[[ -f $NEW_VFSTAB_FILE ]] && cp /dev/null $NEW_VFSTAB_FILE

awk '$4 != "swap" {print $1}' /etc/vfstab > $NEW_VFSTAB_FILE
echo "$voldev\t-\t-\tswap\t-\tno\t-"  >> $NEW_VFSTAB_FILE

# Copy off the original vfstab, and run swapadd on the newly constructed one.
log_must cp $VFSTAB_FILE $PREV_VFSTAB_FILE
log_must cp $NEW_VFSTAB_FILE $VFSTAB_FILE
log_must swapadd $VFSTAB_FILE

log_must file_write -o create -f $TESTDIR/$TESTFILE \
    -b $BLOCKSZ -c $NUM_WRITES -d $DATA

[[ ! -f $TESTDIR/$TESTFILE ]] &&
    log_fail "Unable to create file under $TESTDIR"

filesize=`ls -l $TESTDIR/$TESTFILE | awk '{print $5}'`
tf_size=$((BLOCKSZ * NUM_WRITES))
(($tf_size != $filesize)) && \
    log_fail "testfile is ($filesize bytes), expected ($tf_size bytes)"

log_pass "Successfully added a zvol to swap area through /etc/vfstab."
