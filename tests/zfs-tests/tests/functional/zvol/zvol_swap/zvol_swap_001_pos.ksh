#!/bin/ksh -p
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
# Verify that a zvol can be used as a swap device
#
# STRATEGY:
# 1. Create a pool
# 2. Create a zvol volume
# 3. Use zvol as swap space
# 4. Create a file under /var/tmp (TEST_BASE_DIR)
#

verify_runnable "global"

function cleanup
{
	rm -rf $TEMPFILE

	if is_swap_inuse $voldev ; then
		log_must swap_cleanup $voldev
	fi
}

log_assert "Verify that a zvol can be used as a swap device"

log_onexit cleanup

voldev=${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL
log_note "Add zvol volume as swap space"
log_must swap_setup $voldev

log_note "Create a file under $TEST_BASE_DIR"
log_must file_write -o create -f $TEMPFILE \
    -b $BLOCKSZ -c $NUM_WRITES -d $DATA

[[ ! -f $TEMPFILE ]] && log_fail "Unable to create file under $TEST_BASE_DIR"

filesize=`ls -l $TEMPFILE | awk '{print $5}'`
tf_size=$(( BLOCKSZ * NUM_WRITES ))
(( $tf_size != $filesize )) &&
    log_fail "testfile is ($filesize bytes), expected ($tf_size bytes)"

log_pass "Successfully added a zvol to swap area."
